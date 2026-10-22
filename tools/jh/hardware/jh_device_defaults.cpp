#include "jh_device_defaults.h"
#include <algorithm>
#include <jb/json_number.h>
#include <map>
#include <utility>
#include <vector>

namespace jh::hardware {

namespace {
bool upper(char c) { return c >= 'A' && c <= 'Z'; }
bool lower(char c) { return c >= 'a' && c <= 'z'; }
bool digit(char c) { return c >= '0' && c <= '9'; }
bool alnum(char c) { return upper(c) || lower(c) || digit(c); }

[[noreturn]] void defaults_error(const std::string &message) {
  throw failure{HAL_ECONFIG, 2, message, {}, {}, {}};
}

struct literal_error {
  std::string message;
};

/* Why jb_c_literal refused a value, in the words of the Python generator. */
std::string literal_failure(hal_status_t status, view value,
                            const c_type_info &type) {
  const std::string token = value.raw();
  if (status == HAL_ETYPE) {
    if (value.is(JB_JSON_NUMBER) && type.category == c_kind::integer)
      return token + " is not an integer";
    return token + " is not a " + json_type_name(type.category);
  }
  if (status == HAL_EOVERFLOW && type.category == c_kind::floating) {
    size_t bytes;
    if (jb_json_float_workspace_size(value.node, type.bits, &bytes) == HAL_OK) {
      std::vector<unsigned char> scratch(bytes);
      const jb_json_float_options_t options{false, true,
                                            64 * token.size() + 1000000};
      uint32_t bits32;
      uint64_t bits64;
      const hal_status_t rounded =
          type.bits == 32
              ? jb_json_get_f32_bits(value.node, &options,
                                     {scratch.data(), scratch.size()}, &bits32)
              : jb_json_get_f64_bits(value.node, &options,
                                     {scratch.data(), scratch.size()}, &bits64);
      if (rounded == HAL_OK)
        return token + " rounds to zero in " + type.name;
    }
    return token + " overflows " + type.name;
  }
  if (status == HAL_EOVERFLOW)
    return token + " does not fit " + type.name;
  return token + ": " + hal_status_to_string(status);
}

/* C spelling of one value through jb_c_literal. */
std::string c_literal(view value, const c_type_info &type) {
  size_t scratch_bytes;
  hal_status_t status =
      jb_c_literal_workspace_size(value.node, type.literal, &scratch_bytes);
  if (status != HAL_OK)
    throw literal_error{literal_failure(status, value, type)};
  std::vector<unsigned char> scratch(scratch_bytes);
  std::string text(256, '\0');
  size_t required = 0;
  const size_t budget = 64 * value.raw().size() + 1000000;
  status =
      jb_c_literal(value.node, type.literal, {scratch.data(), scratch.size()},
                   budget, text.data(), text.size(), &required);
  if (status == HAL_EOVERFLOW && required > text.size()) {
    text.resize(required);
    status =
        jb_c_literal(value.node, type.literal, {scratch.data(), scratch.size()},
                     budget, text.data(), text.size(), &required);
  }
  if (status != HAL_OK)
    throw literal_error{literal_failure(status, value, type)};
  text.resize(required);
  return text;
}

bool is_string_enum(view definition) {
  view ignored;
  return definition.member("cType").string() == "string" &&
         definition.find("enum", &ignored);
}

std::string enum_macro(const std::string &compatible, const std::string &prop,
                       const std::string &value) {
  return "JH_ENUM_" + macro_token(compatible) + "_" + macro_token(prop) + "_" +
         macro_token(value);
}

using values = std::map<std::string, view>;

values defaults_of(const binding &type) {
  values result;
  view properties;
  if (type.root().find("properties", &properties))
    for (view definition : properties.children()) {
      view value;
      if (definition.find("default", &value))
        result.emplace(definition.key(), value);
    }
  return result;
}

struct group {
  std::string prefix;
  const binding *type;
  values entries;
  std::string origin;
};

class renderer {
  const catalogue &types_;
  std::vector<std::string> lines_;
  std::map<std::string, std::string> origins_;

  void define(const std::string &name, const std::string &value,
              const std::string &origin) {
    const auto [existing, inserted] = origins_.emplace(name, origin);
    if (!inserted)
      defaults_error(name + " is produced by " + existing->second + " and " +
                     origin);
    lines_.push_back("#define " + name + " " + value);
  }

  void emit_defaults(const group &entry) {
    view properties;
    const bool typed = entry.type->root().find("properties", &properties);
    for (const auto &[prop, value] : entry.entries) {
      view definition;
      if (!typed || !properties.find(prop.c_str(), &definition))
        defaults_error(entry.origin + "." + prop + ": not a property of " +
                       entry.type->compatible);
      std::string literal;
      try {
        if (is_string_enum(definition)) {
          literal = enum_macro(entry.type->compatible, prop, value.string());
        } else {
          const c_type_info *type = c_type(definition.member("cType").string());
          if (type == nullptr)
            throw literal_error{"unknown C type"};
          literal = c_literal(value, *type);
        }
      } catch (const literal_error &error) {
        defaults_error(entry.origin + "." + prop + ": " + error.message);
      }
      define(entry.prefix + "_" + macro_token(prop), literal,
             entry.origin + "." + prop);
    }
  }

  void emit_enums(const binding &type) {
    std::vector<std::pair<std::string, view>> enums;
    view properties;
    if (type.root().find("properties", &properties))
      for (view definition : properties.children())
        if (is_string_enum(definition))
          enums.emplace_back(definition.key(), definition);
    std::sort(enums.begin(), enums.end(),
              [](const auto &a, const auto &b) { return a.first < b.first; });
    if (!enums.empty()) {
      lines_.emplace_back();
      lines_.push_back("/* " + type.compatible + ": enum positions */");
    }
    for (const auto &[prop, definition] : enums) {
      size_t index = 0;
      for (view member : definition.member("enum").children()) {
        define(enum_macro(type.compatible, prop, member.string()),
               std::to_string(index) + "U", type.compatible + "." + prop);
        ++index;
      }
    }
  }

  void emit_type(const binding &type) {
    const std::string prefix = "JH_DEFAULT_" + macro_token(type.compatible);
    std::vector<group> groups;
    groups.push_back({prefix, &type, defaults_of(type), type.compatible});
    std::vector<std::pair<std::string, view>> children;
    view declared;
    if (type.root().find("children", &declared))
      for (view child : declared.children())
        children.emplace_back(child.key(), child);
    std::sort(children.begin(), children.end(),
              [](const auto &a, const auto &b) { return a.first < b.first; });
    for (const auto &[name, child] : children) {
      const std::string compatible = child.member("compatible").string();
      const binding *child_type = types_.find(compatible);
      if (child_type == nullptr)
        defaults_error(type.compatible + " child " + name + ": unknown type " +
                       compatible);
      values merged = defaults_of(*child_type);
      view overrides;
      if (child.find("properties", &overrides))
        for (view value : overrides.children())
          merged.insert_or_assign(value.key(), value);
      groups.push_back({prefix + "_" + macro_token(name), child_type,
                        std::move(merged), type.compatible + " child " + name});
    }
    bool any = false;
    for (const auto &entry : groups)
      any = any || !entry.entries.empty();
    if (!any)
      return;
    lines_.emplace_back();
    lines_.push_back("/* " + type.compatible + " */");
    for (const auto &entry : groups)
      emit_defaults(entry);
  }

public:
  explicit renderer(const catalogue &types) : types_(types) {}

  std::string render() {
    lines_ = {"#pragma once",
              "/* Generated from config/hardware/bindings by the device "
              "defaults generator;",
              " * do not edit. */"};
    const auto sorted = types_.sorted();
    for (const binding *type : sorted)
      emit_enums(*type);
    for (const binding *type : sorted)
      emit_type(*type);
    std::string text;
    for (const auto &line : lines_) {
      text += line;
      text += '\n';
    }
    return text;
  }
};
} // namespace

std::string macro_token(const std::string &name) {
  std::string split;
  for (size_t i = 0; i < name.size(); ++i) {
    const char c = name[i];
    if (i > 0 && i + 1 < name.size() && upper(c) && lower(name[i + 1]) &&
        upper(name[i - 1]))
      split += '_';
    split += c;
  }
  std::string boundaries;
  for (size_t i = 0; i < split.size(); ++i) {
    const char c = split[i];
    if (i > 0 && upper(c) && (lower(split[i - 1]) || digit(split[i - 1])))
      boundaries += '_';
    boundaries += c;
  }
  std::string token;
  for (const char c : boundaries) {
    if (alnum(c)) {
      token += upper(c) || digit(c) ? c : static_cast<char>(c - 'a' + 'A');
    } else if (token.empty() || token.back() != '_') {
      token += '_';
    }
  }
  if (!token.empty() && token.front() == '_')
    token.erase(0, 1);
  if (!token.empty() && token.back() == '_')
    token.pop_back();
  return token;
}

std::string render_device_defaults(const catalogue &types) {
  return renderer(types).render();
}

} // namespace jh::hardware
