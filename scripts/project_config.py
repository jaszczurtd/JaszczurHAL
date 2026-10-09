#!/usr/bin/env python3
"""Read a project's hal_project_config.h the way the C preprocessor does.

The header is the single description of a project's build configuration:
HAL features, values the build reads, and named build variants declared with
``JH_PROJECT_VARIANTS(X)``. Every tool (CMake, jh-vscode, the feature lint,
ESP-IDF) reads it through this module, so all of them see what the compiler
sees for the same target, extra definitions and variant.

The evaluator covers what a macro-only header needs: ``#define``/``#undef`` of
object-like and function-like macros, ``#if``/``#ifdef``/``#ifndef``/``#elif``/
``#elifdef``/``#elifndef``/``#else``/``#endif`` with ``defined`` and integer
expressions, quoted ``#include`` relative to the including file, ``#pragma
once`` and ``#error``. A condition may depend only on what the build passes to
the compiler before the header (target selector, extra and variant
definitions) and on the header's own macros; a macro the build reads that
depends on anything else is reported by ``uncertain``. Integer arithmetic is
signed and unbounded: a comparison that mixes a negative value with an
unsigned one gives the mathematical result, not the C conversion, and results
do not wrap at 64 bits. Operands C skips (after a decided ``&&``/``||``, the
``?:`` branch not taken) are parsed but not computed.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
import re
from typing import Any, Iterable, Iterator, Sequence

HEADER_NAME = "hal_project_config.h"
VARIANTS_MACRO = "JH_PROJECT_VARIANTS"
TARGETS_MACRO = "JH_PROJECT_TARGETS"
DECLARATION_MACROS = (VARIANTS_MACRO, TARGETS_MACRO)
TARGET_SELECTORS = (
    "HAL_TARGET_RP2040",
    "HAL_TARGET_RP2350_ARM",
    "HAL_TARGET_RP2350_RISCV",
    "HAL_TARGET_STM32G474",
    "HAL_TARGET_ESP32",
    "HAL_TARGET_ESP32_S3",
    "HAL_TARGET_MOCK",
)
FEATURE_PATTERN = re.compile(r"HAL_(?:ENABLE|DISABLE)_[A-Z0-9_]+")
IDENTIFIER = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
# Variant ids name build directories and modules; one letter case keeps them
# distinct on case-insensitive file systems.
VARIANT_ID = re.compile(r"[A-Z][A-Z0-9_]*")
DEFINITION_ARGUMENT = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)(?:=(.*))?", re.S)
# A variant definition is C source, so a formatter may space out the "=".
VARIANT_DEFINITION = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)(?:\s*=\s*(.*))?", re.S)
MAX_INCLUDE_DEPTH = 16


class ProjectConfigError(ValueError):
    """Header the reader cannot evaluate, with a [JH-CFG-*] diagnostic."""


class ConfigurationRejected(ProjectConfigError):
    """The header stops this build on ``#error``."""


@dataclass(frozen=True)
class Macro:
    name: str
    body: str
    parameters: tuple[str, ...] | None
    source: str

    @property
    def function_like(self) -> bool:
        return self.parameters is not None


@dataclass(frozen=True)
class ProjectVariant:
    id: str
    description: str
    definitions: tuple[str, ...]
    source: str


@dataclass
class ProjectConfig:
    """Result of evaluating the header for one set of predefined macros."""

    macros: dict[str, Macro]
    variants: tuple[ProjectVariant, ...]
    files: tuple[Path, ...]
    error: tuple[str, str] | None = None
    dependencies: dict[str, frozenset[str]] = field(default_factory=dict)
    targets: tuple[str, ...] = ()

    def defined(self, name: str) -> bool:
        return name in self.macros

    def value(self, name: str) -> str | None:
        macro = self.macros.get(name)
        return None if macro is None else macro.body

    def integer(self, name: str) -> int | None:
        """Integer value of an object-like macro, or None when it has none or
        uses a name this configuration does not define."""
        macro = self.macros.get(name)
        if macro is None or macro.function_like or not macro.body or self.unresolved(name):
            return None
        try:
            return evaluate_expression(macro.body, self.macros, macro.source)
        except ProjectConfigError:
            return None

    def uncertain(self, name: str) -> frozenset[str]:
        """Identifiers the reader cannot see that decide whether or how
        ``name`` is defined; empty when the result is exact."""
        return self.dependencies.get(name, frozenset())

    def unresolved(self, name: str) -> frozenset[str]:
        """Names the value of ``name`` uses that this configuration cannot
        resolve: names it does not define (SDK macros, C symbols), function-like
        macros, and the unknown names deciding a macro the value uses. The
        compiler computes such a value later, so the reader has none."""
        macro = self.macros.get(name)
        if macro is None or macro.function_like or not macro.body:
            return frozenset()
        names: set[str] = set()
        _referenced_names(_pp_names(macro.body), self.macros, frozenset({name}), names)
        unresolved = {
            item
            for item in names
            if item not in self.macros or self.macros[item].function_like
        }
        for item in names:
            unresolved |= self.dependencies.get(item, frozenset())
        return frozenset(unresolved)

    def features(self) -> list[Macro]:
        """HAL feature definitions made by the header itself."""
        return [
            macro
            for name, macro in self.macros.items()
            if FEATURE_PATTERN.fullmatch(name) and macro.source != "predefined"
        ]


def preprocessor_logical_lines(text: str) -> Iterable[tuple[int, str]]:
    """Yield comment-free preprocessing lines and their source line."""
    text = text.replace("\r\n", "\n").replace("\r", "\n")
    spliced: list[str] = []
    source_lines: list[int] = []
    source_line = 1
    source_offset = 0
    while source_offset < len(text):
        if text.startswith("\\\n", source_offset):
            source_line += 1
            source_offset += 2
            continue
        character = text[source_offset]
        spliced.append(character)
        source_lines.append(source_line)
        if character == "\n":
            source_line += 1
        source_offset += 1
    text = "".join(spliced)

    buffer: list[str] = []
    origin_line: int | None = None
    offset = 0
    quote: str | None = None

    def append(character: str) -> None:
        nonlocal origin_line
        if origin_line is None and not character.isspace():
            origin_line = source_lines[offset]
        buffer.append(character)

    while offset < len(text):
        character = text[offset]
        if quote is not None:
            append(character)
            if character == "\\" and offset + 1 < len(text):
                offset += 1
                append(text[offset])
            elif character == quote:
                quote = None
            elif character == "\n":
                yield origin_line or source_lines[offset], "".join(buffer)
                buffer.clear()
                origin_line = None
                quote = None
            offset += 1
            continue

        if text.startswith("//", offset):
            append(" ")
            newline = text.find("\n", offset + 2)
            offset = len(text) if newline < 0 else newline
            continue
        if text.startswith("/*", offset):
            append(" ")
            block_end = text.find("*/", offset + 2)
            if block_end < 0:
                offset = len(text)
                continue
            offset = block_end + 2
            continue
        if character in {'"', "'"}:
            quote = character
            append(character)
            offset += 1
            continue
        if character == "\n":
            yield origin_line or source_lines[offset], "".join(buffer)
            buffer.clear()
            origin_line = None
            offset += 1
            continue
        append(character)
        offset += 1

    if buffer:
        final_line = source_lines[-1] if source_lines else 1
        yield origin_line or final_line, "".join(buffer)


# ── Expressions ──────────────────────────────────────────────────────────────

_TOKEN = re.compile(
    r"\s*(?:"
    r"(?P<number>(?:0[xX][0-9A-Fa-f]+|0[bB][01]+|\d+)[uUlL]*)"
    r"|(?P<char>'(?:\\.|[^'\\])+')"
    r"|(?P<name>[A-Za-z_][A-Za-z0-9_]*)"
    r"|(?P<op>\|\||&&|==|!=|<=|>=|<<|>>|[-+*/%<>!~&|^?:(),])"
    r")"
)
_CHAR_ESCAPES = {"n": 10, "t": 9, "r": 13, "0": 0, "\\": 92, "'": 39, '"': 34}


def _tokens(text: str, source: str) -> list[str]:
    tokens: list[str] = []
    offset = 0
    text = text.strip()
    while offset < len(text):
        match = _TOKEN.match(text, offset)
        if match is None or match.end() == offset:
            raise ProjectConfigError(
                f"{source}: [JH-CFG-SCOPE] cannot evaluate {text[offset:]!r}"
            )
        tokens.append(match.group(match.lastgroup))
        offset = match.end()
    return tokens


def _expand(
    tokens: Sequence[str], macros: dict[str, Macro], source: str, hidden: frozenset[str]
) -> list[str]:
    """Replace ``defined`` operators and object-like macros, C-style."""
    result: list[str] = []
    index = 0
    while index < len(tokens):
        token = tokens[index]
        if token == "defined":
            name, index = _defined_operand(tokens, index + 1, source)
            result.append("1" if name in macros else "0")
            continue
        macro = macros.get(token)
        if macro is not None and token not in hidden:
            if macro.function_like:
                raise ProjectConfigError(
                    f"{source}: [JH-CFG-SCOPE] function-like macro {token} "
                    "is not supported in conditions"
                )
            body = _tokens(macro.body, macro.source) if macro.body else []
            result.extend(_expand(body, macros, source, hidden | {token}))
        else:
            result.append(token)
        index += 1
    return result


def _defined_operand(tokens: Sequence[str], index: int, source: str) -> tuple[str, int]:
    parenthesized = index < len(tokens) and tokens[index] == "("
    if parenthesized:
        index += 1
    if index >= len(tokens) or not IDENTIFIER.fullmatch(tokens[index]):
        raise ProjectConfigError(f"{source}: [JH-CFG-SCOPE] malformed defined()")
    name = tokens[index]
    index += 1
    if parenthesized:
        if index >= len(tokens) or tokens[index] != ")":
            raise ProjectConfigError(f"{source}: [JH-CFG-SCOPE] malformed defined()")
        index += 1
    return name, index


def _integer_literal(token: str) -> int:
    if token.startswith("'"):
        body = token[1:-1]
        if body.startswith("\\"):
            return _CHAR_ESCAPES.get(body[1], ord(body[1]))
        return ord(body[0])
    digits = token.rstrip("uUlL")
    if digits[:2] in {"0x", "0X"}:
        return int(digits, 16)
    if digits[:2] in {"0b", "0B"}:
        return int(digits[2:], 2)
    if len(digits) > 1 and digits.startswith("0"):
        return int(digits, 8)
    return int(digits)


class _Parser:
    """Precedence climbing over expanded tokens; C semantics for integers."""

    _BINARY = (
        ("||",), ("&&",), ("|",), ("^",), ("&",), ("==", "!="),
        ("<", ">", "<=", ">="), ("<<", ">>"), ("+", "-"), ("*", "/", "%"),
    )

    def __init__(self, tokens: list[str], source: str) -> None:
        self.tokens = tokens
        self.index = 0
        self.source = source

    def fail(self, detail: str) -> ProjectConfigError:
        return ProjectConfigError(f"{self.source}: [JH-CFG-SCOPE] {detail}")

    def peek(self) -> str | None:
        return self.tokens[self.index] if self.index < len(self.tokens) else None

    def take(self, expected: str | None = None) -> str:
        token = self.peek()
        if token is None or (expected is not None and token != expected):
            raise self.fail(f"expected {expected or 'operand'} in condition")
        self.index += 1
        return token

    def parse(self) -> int:
        value = self.conditional(True)
        if self.peek() is not None:
            raise self.fail(f"unexpected {self.peek()!r} in condition")
        return value

    # ``live`` is False for an operand C does not evaluate: the right side of a
    # decided && or ||, and the branch ?: does not take.
    def conditional(self, live: bool) -> int:
        condition = self.binary(0, live)
        if self.peek() != "?":
            return condition
        self.take("?")
        when_true = self.conditional(live and bool(condition))
        self.take(":")
        when_false = self.conditional(live and not condition)
        return when_true if condition else when_false

    def binary(self, level: int, live: bool) -> int:
        if level == len(self._BINARY):
            return self.unary(live)
        value = self.binary(level + 1, live)
        while self.peek() in self._BINARY[level]:
            operator = self.take()
            decided = (operator == "&&" and not value) or (operator == "||" and value)
            right_live = live and not decided
            right = self.binary(level + 1, right_live)
            value = self.apply(operator, value, right, live)
        return value

    def apply(self, operator: str, left: int, right: int, live: bool = True) -> int:
        if not live:
            return 0  # C parses an operand it skips but computes nothing.
        if operator in {"/", "%"} and right == 0:
            raise self.fail("division by zero in condition")
        if operator in {"<<", ">>"} and right < 0:
            # Negative counts follow GCC's preprocessor extension.
            operator, right = ("<<" if operator == ">>" else ">>"), -right
        if operator == "/":
            quotient = abs(left) // abs(right)
            return quotient if (left < 0) == (right < 0) else -quotient
        if operator == "%":
            return left - right * self.apply("/", left, right)
        return {
            "||": lambda: int(bool(left) or bool(right)),
            "&&": lambda: int(bool(left) and bool(right)),
            "|": lambda: left | right, "^": lambda: left ^ right,
            "&": lambda: left & right, "==": lambda: int(left == right),
            "!=": lambda: int(left != right), "<": lambda: int(left < right),
            ">": lambda: int(left > right), "<=": lambda: int(left <= right),
            ">=": lambda: int(left >= right), "<<": lambda: left << right,
            ">>": lambda: left >> right, "+": lambda: left + right,
            "-": lambda: left - right, "*": lambda: left * right,
        }[operator]()

    def unary(self, live: bool) -> int:
        token = self.take()
        if token == "!":
            return int(not self.unary(live))
        if token == "~":
            return ~self.unary(live)
        if token == "-":
            return -self.unary(live)
        if token == "+":
            return self.unary(live)
        if token == "(":
            value = self.conditional(live)
            self.take(")")
            return value
        if token[0].isdigit() or token.startswith("'"):
            return _integer_literal(token)
        if IDENTIFIER.fullmatch(token):
            return 0  # An identifier left after expansion is 0, as in C.
        raise self.fail(f"unexpected {token!r} in condition")


def evaluate_expression(text: str, macros: dict[str, Macro], source: str) -> int:
    return _Parser(_expand(_tokens(text, source), macros, source, frozenset()), source).parse()


_PP_TOKEN = re.compile(
    r"(?P<number>\.?\d(?:[eEpP][+-]|[A-Za-z0-9_.])*)"
    r"|(?P<literal>\"(?:\\.|[^\"\\])*\"|'(?:\\.|[^'\\])*')"
    r"|(?P<name>[A-Za-z_][A-Za-z0-9_]*)"
    r"|(?P<other>\S)",
    re.S,
)


def _pp_names(text: str) -> list[str]:
    """Names and punctuators of a directive argument or a macro body; numbers
    (``1u``, ``0x10``) and character or string literals are not names."""
    return [
        match.group(match.lastgroup)
        for match in _PP_TOKEN.finditer(text)
        if match.lastgroup in {"name", "other"}
    ]


def _referenced_names(
    tokens: Sequence[str],
    macros: dict[str, Macro],
    hidden: frozenset[str],
    found: set[str],
) -> None:
    """Add every name ``tokens`` use to ``found``: the operand of ``defined``,
    and each object-like macro with the names its body expands to."""
    index = 0
    while index < len(tokens):
        token = tokens[index]
        if token == "defined":
            index += 1
            if index < len(tokens) and tokens[index] == "(":
                index += 1
            if index < len(tokens) and IDENTIFIER.fullmatch(tokens[index]):
                found.add(tokens[index])
            index += 1
            continue
        if IDENTIFIER.fullmatch(token):
            found.add(token)
            macro = macros.get(token)
            if macro is not None and token not in hidden and not macro.function_like:
                _referenced_names(_pp_names(macro.body), macros, hidden | {token}, found)
        index += 1


# ── Variants ─────────────────────────────────────────────────────────────────

def _split_arguments(text: str) -> list[str]:
    """Split a macro argument list on top-level commas."""
    arguments: list[str] = []
    depth = 0
    quote: str | None = None
    current: list[str] = []
    index = 0
    while index < len(text):
        character = text[index]
        if quote is not None:
            current.append(character)
            if character == "\\" and index + 1 < len(text):
                index += 1
                current.append(text[index])
            elif character == quote:
                quote = None
        elif character in {'"', "'"}:
            quote = character
            current.append(character)
        elif character == "(":
            depth += 1
            current.append(character)
        elif character == ")":
            depth -= 1
            current.append(character)
        elif character == "," and depth == 0:
            arguments.append("".join(current).strip())
            current = []
        else:
            current.append(character)
        index += 1
    arguments.append("".join(current).strip())
    return arguments


def _x_entries(macro: Macro) -> Iterator[str]:
    """Yield the argument text of each ``X(...)`` entry of a declaration
    X-macro such as ``JH_PROJECT_VARIANTS(X)``."""
    (callback,) = macro.parameters or ("",)
    body = macro.body
    position = 0
    pattern = re.compile(rf"\s*{re.escape(callback)}\s*\(")
    while position < len(body.rstrip()):
        match = pattern.match(body, position)
        if match is None:
            raise ProjectConfigError(
                f"{macro.source}: [JH-CFG-VARIANT] {macro.name} must list "
                f"only {callback}(...) entries"
            )
        depth = 1
        index = match.end()
        quote: str | None = None
        while index < len(body) and depth:
            character = body[index]
            if quote is not None:
                if character == "\\":
                    index += 1
                elif character == quote:
                    quote = None
            elif character in {'"', "'"}:
                quote = character
            elif character == "(":
                depth += 1
            elif character == ")":
                depth -= 1
            index += 1
        if depth:
            raise ProjectConfigError(
                f"{macro.source}: [JH-CFG-VARIANT] unbalanced {callback}(...) entry"
            )
        yield body[match.end() : index - 1]
        position = index


def parse_variants(macro: Macro | None) -> tuple[ProjectVariant, ...]:
    if macro is None:
        return ()
    if macro.parameters is None or len(macro.parameters) != 1:
        raise ProjectConfigError(
            f"{macro.source}: [JH-CFG-VARIANT] {VARIANTS_MACRO} must take one "
            "parameter, e.g. JH_PROJECT_VARIANTS(X)"
        )
    variants: list[ProjectVariant] = []
    seen: set[str] = set()
    for entry in _x_entries(macro):
        arguments = _split_arguments(entry)
        if len(arguments) < 3:
            raise ProjectConfigError(
                f"{macro.source}: [JH-CFG-VARIANT] a variant needs an id, a "
                f"description and at least one definition: {entry!r}"
            )
        variant_id, description, *definitions = arguments
        if not VARIANT_ID.fullmatch(variant_id) or variant_id in seen:
            raise ProjectConfigError(
                f"{macro.source}: [JH-CFG-VARIANT] variant id {variant_id!r} "
                "must be unique and use capital letters, digits and _, "
                "starting with a letter"
            )
        if not re.fullmatch(r'"(?:\\.|[^"\\])+"', description):
            raise ProjectConfigError(
                f"{macro.source}: [JH-CFG-VARIANT] variant {variant_id} needs "
                "a non-empty string description"
            )
        normalized: list[str] = []
        for definition in definitions:
            match = VARIANT_DEFINITION.fullmatch(definition)
            if match is None or match.group(1) in TARGET_SELECTORS:
                raise ProjectConfigError(
                    f"{macro.source}: [JH-CFG-VARIANT] variant {variant_id} has "
                    f"invalid definition {definition!r}"
                )
            name, value = match.groups()
            # The -D form: the compiler takes no spaces around "=".
            normalized.append(name if value is None else f"{name}={value}")
        seen.add(variant_id)
        variants.append(
            ProjectVariant(
                variant_id,
                description[1:-1],
                tuple(normalized),
                macro.source,
            )
        )
    return tuple(variants)


def parse_targets(macro: Macro | None) -> tuple[str, ...]:
    """Target selectors listed by ``JH_PROJECT_TARGETS(X)``."""
    if macro is None:
        return ()
    if macro.parameters is None or len(macro.parameters) != 1:
        raise ProjectConfigError(
            f"{macro.source}: [JH-CFG-VARIANT] {TARGETS_MACRO} must take one "
            "parameter, e.g. JH_PROJECT_TARGETS(X)"
        )
    selectors: list[str] = []
    for entry in _x_entries(macro):
        selector = entry.strip()
        if selector not in TARGET_SELECTORS or selector in selectors:
            raise ProjectConfigError(
                f"{macro.source}: [JH-CFG-VARIANT] {TARGETS_MACRO} entry "
                f"{selector!r} must be one HAL_TARGET_* selector, listed once"
            )
        selectors.append(selector)
    if not selectors:
        raise ProjectConfigError(
            f"{macro.source}: [JH-CFG-VARIANT] {TARGETS_MACRO} lists no target"
        )
    return tuple(selectors)


# ── Evaluation ───────────────────────────────────────────────────────────────

_DIRECTIVE = re.compile(r"^\s*#\s*([A-Za-z_]+)\b(.*)$", re.S)
_DEFINE = re.compile(r"\s*([A-Za-z_][A-Za-z0-9_]*)(\([^)]*\))?(.*)$", re.S)


@dataclass
class _Frame:
    parent_active: bool
    active: bool
    taken: bool
    unknown: set[str]


@dataclass
class _Line:
    path: Path
    number: int
    text: str

    @property
    def source(self) -> str:
        return f"{self.path}:{self.number}"


def predefined_macros(definitions: Iterable[str]) -> dict[str, Macro]:
    """Macros passed with -D: ``NAME`` (value 1) or ``NAME=VALUE``."""
    macros: dict[str, Macro] = {}
    for definition in definitions:
        match = DEFINITION_ARGUMENT.fullmatch(definition.strip())
        if match is None:
            raise ProjectConfigError(
                f"predefined: [JH-CFG-VALUE] invalid definition {definition!r}"
            )
        value = match.group(2)
        macros[match.group(1)] = Macro(
            match.group(1), "1" if value is None else value.strip(), None, "predefined"
        )
    return macros


class _Reader:
    def __init__(self, macros: dict[str, Macro], known: set[str]) -> None:
        self.macros = macros
        self.known = known
        self.stack: list[_Frame] = []
        self.files: list[Path] = []
        self.once: set[Path] = set()
        self.dependencies: dict[str, set[str]] = {}
        self.error: tuple[str, str] | None = None

    @property
    def active(self) -> bool:
        return not self.stack or self.stack[-1].active

    def is_known(self, name: str) -> bool:
        return name in self.known or FEATURE_PATTERN.fullmatch(name) is not None

    def unknown_in(self, text: str) -> set[str]:
        """Names the reader cannot see that decide ``text`` here, through the
        macros it expands to and the conditions that defined them."""
        names: set[str] = set()
        _referenced_names(_pp_names(text), self.macros, frozenset(), names)
        unknown = {name for name in names if not self.is_known(name)}
        for name in names:
            unknown |= self.dependencies.get(name, set())
        return unknown

    def note(self, name: str) -> None:
        """Record what decides ``name`` here; an exact active definition
        replaces an earlier uncertain one."""
        unknown = set().union(*(frame.unknown for frame in self.stack))
        if unknown:
            self.dependencies.setdefault(name, set()).update(unknown)
        elif self.active:
            self.dependencies.pop(name, None)

    def read(self, path: Path, depth: int = 0) -> None:
        path = path.resolve()
        if path in self.once or self.error:
            return
        if depth > MAX_INCLUDE_DEPTH:
            raise ProjectConfigError(f"{path}: [JH-CFG-SCOPE] includes nest too deeply")
        if path not in self.files:
            self.files.append(path)
        try:
            text = path.read_text(encoding="utf-8")
        except OSError as error:
            raise ProjectConfigError(f"{path}: [JH-CFG-SCOPE] cannot read: {error}") from error
        base_depth = len(self.stack)
        for number, text_line in preprocessor_logical_lines(text):
            match = _DIRECTIVE.match(text_line)
            if match is not None:
                self.directive(match.group(1), match.group(2).strip(), _Line(path, number, text_line), depth)
            if self.error:
                return
        if len(self.stack) != base_depth:
            raise ProjectConfigError(f"{path}: [JH-CFG-SCOPE] unterminated #if")

    def condition(self, kind: str, argument: str, line: _Line) -> bool:
        if kind in {"ifdef", "ifndef", "elifdef", "elifndef"}:
            if not IDENTIFIER.fullmatch(argument):
                raise ProjectConfigError(f"{line.source}: [JH-CFG-SCOPE] #{kind} needs one name")
            return (argument in self.macros) == (kind in {"ifdef", "elifdef"})
        return bool(evaluate_expression(argument, self.macros, line.source))

    def directive(self, name: str, argument: str, line: _Line, depth: int) -> None:
        if name in {"if", "ifdef", "ifndef"}:
            parent = self.active
            result = parent and self.condition(name, argument, line)
            self.stack.append(_Frame(parent, result, result, self.unknown_in(_tested(name, argument))))
        elif name in {"elif", "elifdef", "elifndef"}:
            frame = self.top(name, line)
            frame.unknown |= self.unknown_in(_tested(name, argument))
            result = frame.parent_active and not frame.taken and self.condition(name, argument, line)
            frame.active = result
            frame.taken = frame.taken or result
        elif name == "else":
            frame = self.top(name, line)
            frame.active = frame.parent_active and not frame.taken
            frame.taken = True
        elif name == "endif":
            self.top(name, line)
            self.stack.pop()
        elif name == "define":
            self.define(argument, line)
        elif name == "undef":
            if not IDENTIFIER.fullmatch(argument):
                raise ProjectConfigError(f"{line.source}: [JH-CFG-SCOPE] #undef needs one name")
            self.note(argument)
            if self.active:
                self.macros.pop(argument, None)
        elif not self.active:
            return
        elif name == "include":
            self.include(argument, line, depth)
        elif name == "error":
            self.error = (argument, line.source)
        elif name == "pragma":
            if argument == "once":
                self.once.add(line.path.resolve())
        elif name not in {"warning", "line"}:
            raise ProjectConfigError(f"{line.source}: [JH-CFG-SCOPE] unsupported #{name}")

    def top(self, name: str, line: _Line) -> _Frame:
        if not self.stack:
            raise ProjectConfigError(f"{line.source}: [JH-CFG-SCOPE] #{name} without #if")
        return self.stack[-1]

    def define(self, argument: str, line: _Line) -> None:
        macro = _parse_define(argument, line.source)
        self.note(macro.name)
        if self.active:
            self.macros[macro.name] = macro

    def include(self, argument: str, line: _Line, depth: int) -> None:
        self.read(_include_path(argument, line), depth + 1)


def _tested(directive: str, argument: str) -> str:
    """The condition a directive tests: ``#ifdef X`` tests ``defined X``."""
    if directive in {"ifdef", "ifndef", "elifdef", "elifndef"}:
        return f"defined {argument}"
    return argument


def _parse_define(argument: str, source: str) -> Macro:
    match = _DEFINE.match(argument)
    if match is None:
        raise ProjectConfigError(f"{source}: [JH-CFG-SCOPE] malformed #define")
    parameters = None
    if match.group(2) is not None:
        parameters = tuple(
            item.strip() for item in match.group(2)[1:-1].split(",") if item.strip()
        )
    return Macro(match.group(1), match.group(3).strip(), parameters, source)


def _include_path(argument: str, line: _Line) -> Path:
    match = re.fullmatch(r'"([^"]+)"', argument)
    if match is None:
        raise ProjectConfigError(
            f"{line.source}: [JH-CFG-SCOPE] only #include \"file\" relative to "
            "the project configuration is supported"
        )
    return line.path.parent / match.group(1)


@dataclass
class _Scan:
    """Syntactic view of the header files, independent of any condition."""

    defined: set[str] = field(default_factory=set)
    declarations: dict[str, Macro] = field(default_factory=dict)


def _include_guard(directives: Sequence[tuple[str, str]]) -> bool:
    """True when the directives wrap the whole file in a classic include
    guard: ``#ifndef G`` and ``#define G`` first, the matching ``#endif`` last
    and no ``#elif``/``#else`` of its own."""
    if len(directives) < 3 or directives[0][0] != "ifndef":
        return False
    guard = directives[0][1]
    defined = _DEFINE.match(directives[1][1])
    if directives[1][0] != "define" or defined is None or defined.group(1) != guard:
        return False
    depth = 0
    for index, (name, _) in enumerate(directives):
        if name in {"if", "ifdef", "ifndef"}:
            depth += 1
        elif depth == 1 and name in {"elif", "elifdef", "elifndef", "else"}:
            return False
        elif name == "endif":
            depth -= 1
            if depth == 0:
                return index == len(directives) - 1
    return False


def _scan(path: Path, scan: _Scan | None = None, seen: set[Path] | None = None, depth: int = 0) -> _Scan:
    scan = _Scan() if scan is None else scan
    seen = set() if seen is None else seen
    path = path.resolve()
    if path in seen or not path.is_file():
        return scan
    seen.add(path)
    directives: list[tuple[str, str, _Line]] = []
    for number, text in preprocessor_logical_lines(path.read_text(encoding="utf-8")):
        match = _DIRECTIVE.match(text)
        if match is not None:
            directives.append(
                (match.group(1), match.group(2).strip(), _Line(path, number, text))
            )
    # Declarations inside the file's own include guard are unconditional.
    nesting = -1 if _include_guard([(name, argument) for name, argument, _ in directives]) else 0
    for name, argument, line in directives:
        if name in {"if", "ifdef", "ifndef"}:
            nesting += 1
        elif name == "endif":
            nesting -= 1
        elif name in {"define", "undef"}:
            defined = _DEFINE.match(argument)
            if defined is None:
                continue
            scan.defined.add(defined.group(1))
            declared = defined.group(1)
            if declared in DECLARATION_MACROS:
                if name != "define" or nesting > 0 or depth or declared in scan.declarations:
                    raise ProjectConfigError(
                        f"{line.source}: [JH-CFG-VARIANT] define {declared} "
                        f"once, unconditionally, in {HEADER_NAME}"
                    )
                scan.declarations[declared] = _parse_define(argument, line.source)
        elif name == "include" and re.fullmatch(r'"([^"]+)"', argument):
            _scan(_include_path(argument, line), scan, seen, depth + 1)
    return scan


def read_project_config(
    header: Path | None, predefined: Iterable[str] = (), preinclude: Path | None = None
) -> ProjectConfig:
    """Evaluate ``header`` with the given -D definitions (``NAME`` or
    ``NAME=VALUE``). ``preinclude`` is read first, as the hook loads the
    generated jh_hardware.h before the project header. A missing header is an
    empty configuration."""
    macros = predefined_macros(predefined)
    early = _scan(preinclude).defined if preinclude is not None else set()
    if header is None or not header.is_file():
        if preinclude is None:
            return ProjectConfig(macros, (), ())
        reader = _Reader(macros, early | set(macros))
        reader.read(preinclude)
        return ProjectConfig(reader.macros, (), tuple(reader.files), reader.error)
    scan = _scan(header)
    variants = parse_variants(scan.declarations.get(VARIANTS_MACRO))
    targets = parse_targets(scan.declarations.get(TARGETS_MACRO))
    switches = {
        item.split("=", 1)[0] for variant in variants for item in variant.definitions
    }
    reader = _Reader(macros, scan.defined | early | set(macros) | set(TARGET_SELECTORS) | switches)
    if preinclude is not None:
        reader.read(preinclude)
    reader.read(header)
    return ProjectConfig(
        reader.macros,
        variants,
        tuple(reader.files),
        reader.error,
        {name: frozenset(value) for name, value in reader.dependencies.items()},
        targets,
    )


# ── Project builds ───────────────────────────────────────────────────────────

REPOSITORY_ROOT = Path(__file__).resolve().parents[1]


@dataclass(frozen=True)
class TargetFacts:
    id: str
    selector: str
    required_features: tuple[str, ...]


def load_targets(repo_root: Path = REPOSITORY_ROOT) -> dict[str, TargetFacts]:
    """Target selectors and required features from the board registry."""
    from generate_board_config import load_registry

    targets, _, _ = load_registry(repo_root / "boards")
    return {
        target_id: TargetFacts(
            target_id,
            target["hal"]["targetSelector"],
            tuple(target.get("requiredFeatures") or ()),
        )
        for target_id, target in targets.items()
    }


@dataclass(frozen=True)
class BuildConfig:
    """One project configuration: the header evaluated for a target and an
    optional variant, as the compiler sees it."""

    header: Path | None
    target: TargetFacts
    variant: ProjectVariant | None
    extra_definitions: tuple[str, ...]
    config: ProjectConfig

    @property
    def definitions(self) -> tuple[str, ...]:
        """-D values the build passes before the header: extra definitions,
        then the variant's."""
        return (*self.extra_definitions, *(self.variant.definitions if self.variant else ()))

    def definition_sources(self) -> list[tuple[str, str]]:
        """Each definition with the place that asked for it. A feature
        definition the header removes with #undef is left out: the compiler
        sees the header's final state."""
        sources = [
            (definition, f"command-line:--define[{index}]")
            for index, definition in enumerate(self.extra_definitions)
        ]
        if self.variant is not None:
            origin = f"{self.header}:{VARIANTS_MACRO}({self.variant.id})"
            sources.extend((definition, origin) for definition in self.variant.definitions)
        return [
            (definition, source)
            for definition, source in sources
            if not FEATURE_PATTERN.fullmatch(name := definition.split("=", 1)[0])
            or name in self.config.macros
        ]

    def requested_features(self) -> list[str]:
        """Features the project asks for: the header's own and the ones the
        variant or extra definitions name. Target-required features are added
        by the feature resolver."""
        requested = [macro.name for macro in self.config.features()]
        for definition, _ in self.definition_sources():
            name = definition.split("=", 1)[0]
            if FEATURE_PATTERN.fullmatch(name) and name not in requested:
                requested.append(name)
        return requested

    def label(self) -> str:
        target = self.target.id or "default"
        return f"target={target}, variant={self.variant.id if self.variant else 'base'}"


def project_target_ids(config: ProjectConfig, targets: dict[str, TargetFacts]) -> list[str]:
    """Registry IDs of the targets the header declares."""
    by_selector = {facts.selector: target_id for target_id, facts in targets.items()}
    return [by_selector[selector] for selector in config.targets if selector in by_selector]


def macro_directives(path: Path) -> list[tuple[int, str, str]]:
    """(line, "define" or "undef", macro) of every directive of one file,
    active or not."""
    if not path.is_file():
        return []
    found = []
    for number, text in preprocessor_logical_lines(path.read_text(encoding="utf-8")):
        match = _DIRECTIVE.match(text)
        if match is not None and match.group(1) in ("define", "undef"):
            named = _DEFINE.match(match.group(2))
            if named is not None:
                found.append((number, match.group(1), named.group(1)))
    return found


def target_declarations(header: Path) -> list[int]:
    """Lines defining JH_PROJECT_TARGETS, active or not; a device tree takes
    the targets from its assemblies, so any definition is rejected there."""
    return [line for line, kind, name in macro_directives(header)
            if kind == "define" and name == TARGETS_MACRO]


def evaluate_build(
    config_dir: Path | None,
    target: TargetFacts,
    variant_id: str | None = None,
    extra_definitions: Sequence[str] = (),
    hardware_header: Path | None = None,
) -> BuildConfig:
    """Evaluate ``config_dir/hal_project_config.h`` for one build.

    ``extra_definitions`` are -D values a static library build passes; a
    project build passes none. Without ``config_dir`` (a library build with
    no project header) only the definitions count. Raises when the target is
    not declared, the variant is unknown or the header stops on ``#error``.
    """
    header = None if config_dir is None else config_dir / HEADER_NAME
    selector = [target.selector] if target.selector else []
    declarations = read_project_config(header, selector, hardware_header)
    if declarations.targets and target.selector not in declarations.targets:
        declared = ", ".join(declarations.targets)
        raise ProjectConfigError(
            f"{header}: [JH-CFG-TARGET] the project builds for {declared}, "
            f"not {target.selector}"
        )
    variant = None
    if variant_id:
        matches = [item for item in declarations.variants if item.id == variant_id]
        if not matches:
            known = ", ".join(item.id for item in declarations.variants) or "none"
            raise ProjectConfigError(
                f"{header}: [JH-CFG-VARIANT] unknown variant {variant_id!r}; "
                f"declared: {known}"
            )
        variant = matches[0]
    definitions = (*extra_definitions, *(variant.definitions if variant else ()))
    config = read_project_config(
        header, [*selector, *target.required_features, *definitions], hardware_header
    )
    build = BuildConfig(header, target, variant, tuple(extra_definitions), config)
    if config.error is not None:
        message, source = config.error
        raise ConfigurationRejected(
            f"{source}: [JH-CFG-ERROR] {message} ({build.label()})"
        )
    uncertain = {
        name: sorted(config.uncertain(name))
        for name in config.dependencies
        if FEATURE_PATTERN.fullmatch(name)
    }
    for name, unknown in sorted(uncertain.items()):
        raise ProjectConfigError(
            f"{header}: [JH-CFG-SCOPE] {name} depends on {', '.join(unknown)}, "
            "which the build does not pass to the compiler before this header"
        )
    return build


def available_builds(
    config_dir: Path, targets: dict[str, TargetFacts], candidate_targets: Iterable[str]
) -> list[BuildConfig]:
    """Every build the project configures: the declared targets (or the given
    candidates when the header declares none; "" builds with no target) times
    the base and each variant, without the builds that stop on ``#error``."""
    declarations = read_project_config(config_dir / HEADER_NAME)
    target_ids = project_target_ids(declarations, targets) or list(candidate_targets)
    builds: list[BuildConfig] = []
    for target_id in target_ids:
        if target_id and target_id not in targets:
            raise ProjectConfigError(f"[JH-CFG-TARGET] unknown target {target_id!r}")
        facts = targets[target_id] if target_id else TargetFacts("", "", ())
        for variant_id in [None, *(item.id for item in declarations.variants)]:
            try:
                builds.append(evaluate_build(config_dir, facts, variant_id))
            except ConfigurationRejected:
                continue
    return builds


def _cmake_value(value: str) -> str:
    fence = "="
    while f"]{fence}]" in value:
        fence += "="
    return f"[{fence}[{value}]{fence}]"


def cmake_lines(build: BuildConfig, targets: dict[str, TargetFacts]) -> list[str]:
    config = build.config
    values = {
        "JH_PROJECT_CONFIG_HEADER": build.header.as_posix() if build.header else "",
        "JH_PROJECT_CONFIG_FILES": ";".join(path.as_posix() for path in config.files),
        "JH_PROJECT_TARGETS": ";".join(project_target_ids(config, targets)),
        "JH_PROJECT_VARIANTS": ";".join(item.id for item in config.variants),
        "JH_PROJECT_VARIANT": build.variant.id if build.variant else "",
        "JH_PROJECT_DEFINITIONS": ";".join(build.definitions),
        "JH_PROJECT_FEATURES": ";".join(build.requested_features()),
    }
    object_like = sorted(
        name for name, macro in config.macros.items() if not macro.function_like
    )
    values["JH_PROJECT_MACROS"] = ";".join(object_like)
    for name in object_like:
        values[f"JH_PROJECT_DEFINE_{name}"] = config.macros[name].body
        value = config.integer(name)
        if value is not None:
            values[f"JH_PROJECT_INT_{name}"] = str(value)
    for name, unknown in sorted(config.dependencies.items()):
        values[f"JH_PROJECT_UNCERTAIN_{name}"] = ";".join(sorted(unknown))
    for name in object_like:
        unresolved = config.unresolved(name)
        if unresolved:
            values[f"JH_PROJECT_UNRESOLVED_{name}"] = ";".join(sorted(unresolved))
    # The CMake reader function hands exactly these to its caller.
    values["JH_PROJECT_CONFIG_VARIABLES"] = ";".join(values)
    return [
        "# Generated by project_config.py; do not edit.",
        *(f"set({name} {_cmake_value(value)})" for name, value in values.items()),
    ]


def resolve_build_features(build: BuildConfig, model: Any) -> tuple[Any, list[str]]:
    """Resolve the features one build requests against the feature registry
    ``model``: the header's own, the ones its definitions name and the
    target's required ones. Returns the resolution and its findings."""
    import generate_hal_features

    requests = [
        generate_hal_features.FeatureRequest(macro.name, macro.body or None, macro.source)
        for macro in build.config.features()
    ]
    for definition, source in build.definition_sources():
        name, separator, value = definition.partition("=")
        if FEATURE_PATTERN.fullmatch(name):
            requests.append(
                generate_hal_features.FeatureRequest(name, value if separator else None, source)
            )
    return generate_hal_features.resolve_target_feature_requests(
        requests,
        model,
        f"{build.header or HEADER_NAME} [{build.label()}]",
        build.target.id or None,
        {"requiredFeatures": list(build.target.required_features)},
    )


def main(argv: Sequence[str] | None = None) -> int:
    import argparse
    import sys

    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    commands = parser.add_subparsers(dest="command", required=True)
    cmake = commands.add_parser("cmake", help="write one build's configuration for CMake")
    cmake.add_argument("--config-dir", type=Path)
    cmake.add_argument("--target", required=True)
    cmake.add_argument("--variant", default="")
    cmake.add_argument("--define", action="append", default=[])
    cmake.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        targets = load_targets()
        if args.target not in targets:
            raise ProjectConfigError(f"[JH-CFG-TARGET] unknown target {args.target!r}")
        build = evaluate_build(
            args.config_dir.resolve() if args.config_dir else None,
            targets[args.target],
            args.variant or None,
            [definition.removeprefix("-D") for definition in args.define],
        )
        import generate_hal_features

        _, findings = resolve_build_features(
            build, generate_hal_features.load_registry(REPOSITORY_ROOT / "config")
        )
        if findings:
            raise ProjectConfigError("\n".join(findings))
    except (ProjectConfigError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    args.output.parent.mkdir(parents=True, exist_ok=True)
    text = "\n".join(cmake_lines(build, targets)) + "\n"
    if not args.output.is_file() or args.output.read_text(encoding="utf-8") != text:
        args.output.write_text(text, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
