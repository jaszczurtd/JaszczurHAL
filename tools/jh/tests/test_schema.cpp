#include "jh_json_schema.h"
#include <cstdio>
#include <cstring>
#include <jb/array.h>
#include <memory_resource>
#include <string>
#include <vector>

extern "C" hal_status_t
jh_schema_test_unknown_status(void *, const jh_json_schema_error_t *);
extern "C" hal_status_t jh_schema_test_unknown_load(void *, jb_json_span_t,
                                                    jb_json_span_t,
                                                    jh_json_schema_source_t *);

namespace {
int failures = 0;
void require(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "schema: %s\n", what);
    ++failures;
  }
}
struct document {
  std::string input;
  std::vector<jb_json_token_t> tokens;
  jb_json_doc_t doc{};
  explicit document(const char *s) : input(s), tokens(4096) {
    const jb_json_limits_t limits{65536, 4096, 128, 65536, 4096, 4096, 1000000};
    size_t n = 0;
    require(jb_json_parse_workspace_size(&limits, &n) == HAL_OK, "parse size");
    std::vector<unsigned char> scratch(n);
    require(jb_json_parse({input.data(), input.size()}, &limits, tokens.data(),
                          tokens.size(), {scratch.data(), scratch.size()}, &doc,
                          nullptr) == HAL_OK,
            "parse test input");
  }
};
struct report {
  std::string at, uri, rule, code, message;
  jb_json_node_t node;
};
std::string copy(jb_json_span_t s) { return std::string(s.data, s.length); }
hal_status_t collect(void *user, const jh_json_schema_error_t *e) {
  static_cast<std::vector<report> *>(user)->push_back(
      {copy(e->instance_pointer), copy(e->schema_uri), copy(e->schema_pointer),
       copy(e->reason_code), copy(e->message), e->schema_rule});
  return HAL_OK;
}
jh_json_schema_limits_t defaults() {
  return {10000000, 32,   64,   2048,        4096,   65536, 4096,
          4096,     4096, 1024, 1024 * 1024, 100000, 64,    512};
}
struct run {
  document schema, instance;
  std::vector<report> reports;
  jh_json_schema_limits_t limits = defaults();
  explicit run(const char *s, const char *v) : schema(s), instance(v) {}
  hal_status_t validate(jh_json_schema_report_fn callback = collect,
                        void *user = nullptr,
                        jh_json_schema_load_fn loader = nullptr,
                        void *load_user = nullptr) {
    size_t n = 0;
    require(jh_json_schema_workspace_size(&limits, &n) == HAL_OK,
            "workspace query");
    std::vector<unsigned char> scratch(n + 1);
    const jh_json_schema_source_t source{{"root.schema.json", 16}, &schema.doc};
    auto *previous =
        std::pmr::set_default_resource(std::pmr::null_memory_resource());
    const auto status = jh_json_schema_validate(
        &instance.doc, &source, loader, load_user, callback,
        user == nullptr ? &reports : user, {scratch.data() + 1, n}, &limits);
    std::pmr::set_default_resource(previous);
    return status;
  }
};
void equality_and_branches() {
  // Existing Python equality regressions: decimal spelling, bool and object
  // order.
  run equivalent(R"({"const":{"a":1,"b":[2.0]}})", R"({"b":[2],"a":1.0})");
  require(equivalent.validate() == HAL_OK, "structural exact equality");
  run distinct(R"({"const":1})", "true");
  require(distinct.validate() == HAL_ECONFIG, "bool differs from number");
  require(distinct.reports.size() == 1 && distinct.reports[0].code == "const",
          "const report");
  // Binary64 aliases in the old validator must remain distinct exact decimals.
  run large(R"({"const":9007199254740992})", "9007199254740993.0");
  require(large.validate() == HAL_ECONFIG, "large decimal alias differs");
  run fraction(R"({"const":0.1})", "0.10000000000000001");
  require(fraction.validate() == HAL_ECONFIG,
          "fractional decimal alias differs");
  run fractional_integer(R"({"type":"integer"})", "9007199254740992.1");
  require(fractional_integer.validate() == HAL_ECONFIG,
          "fraction does not round to integer");
  run separate(R"({"uniqueItems":true})", "[0.1,0.10000000000000001]");
  require(separate.validate() == HAL_OK, "distinct decimals are unique items");
  run bounded(R"({"maximum":0.1})", "0.10000000000000001");
  require(bounded.validate() == HAL_ECONFIG, "exact decimal bound");
  run enum_alias(R"({"enum":[0.1]})", "0.10000000000000001");
  require(enum_alias.validate() == HAL_ECONFIG,
          "exact decimal enum membership");
  run forms(R"({"oneOf":[{"const":0.1},{"const":0.10000000000000001}]})",
            "0.1");
  require(forms.validate() == HAL_OK, "oneOf distinguishes binary64 aliases");
  run unique(R"({"uniqueItems":true})", "[1,1.0,true]");
  require(unique.validate() == HAL_ECONFIG && unique.reports.size() == 1 &&
              unique.reports[0].at == "/1",
          "exact uniqueItems equality");
  run typed(
      R"({"oneOf":[{"type":"integer","minimum":3},{"type":"string","minLength":2}]})",
      "1");
  require(typed.validate() == HAL_ECONFIG && typed.reports.size() == 1,
          "oneOf branch selection");
  if (typed.reports.size() == 1) {
    const auto &e = typed.reports[0];
    require(e.at.empty() && e.rule == "/oneOf/0/minimum" &&
                e.code == "minimum" && e.message == "below 3",
            "chosen branch metadata");
    require(e.node.doc == &typed.schema.doc &&
                e.node.doc->tokens[e.node.index].type == JB_JSON_NUMBER,
            "actual keyword node");
  }
  run conditional(
      R"({"not":{"type":"null"},"if":{"required":["mode"],"properties":{"mode":{"const":"x"}}},"then":{"maxProperties":1}})",
      R"({"mode":"x","z":0,"a":1})");
  require(conditional.validate() == HAL_ECONFIG &&
              conditional.reports.size() == 1 &&
              conditional.reports[0].at == "/a",
          "discarded not/if diagnostics and kept condition fields");
  run order(
      R"({"properties":{"a":{"items":{"minimum":1}},"fooBar":false,"fooBAR":false}})",
      R"({"fooBar":0,"fooBAR":0,"a":[0,0,0,0,0,0,0,0,0,0,0]})");
  require(order.validate() == HAL_ECONFIG && order.reports.size() == 13,
          "ordered reports");
  if (order.reports.size() == 13)
    require(order.reports[9].at == "/a/9" && order.reports[10].at == "/a/10" &&
                order.reports[11].at == "/fooBAR",
            "numeric pointer order");
}
hal_status_t load_other(void *user, jb_json_span_t base, jb_json_span_t ref,
                        jh_json_schema_source_t *out) {
  require(copy(base) == "root.schema.json" && copy(ref) == "other.schema.json",
          "loader receives document reference without fragment");
  *out = {{"other.schema.json", 17}, &static_cast<document *>(user)->doc};
  return HAL_OK;
}
void references_and_limits() {
  document other(R"({"$defs":{"value":{"minimum":2}}})");
  run external(
      R"({"allOf":[{"$ref":"other.schema.json#/$defs/value"},{"$ref":"other.schema.json#/$defs/value"}]})",
      "1");
  external.limits.max_schema_nodes =
      external.schema.doc.token_count + other.doc.token_count;
  require(external.validate(collect, nullptr, load_other, &other) ==
                  HAL_ECONFIG &&
              external.reports.size() == 2,
          "unique external documents counted once");
  if (external.reports.size() == 2)
    require(external.reports[0].uri == "other.schema.json" &&
                external.reports[0].rule == "/$defs/value/minimum",
            "external rule identity");
  run unknown(R"({"$defs":{"unused":{"dependentRequired":{}}}})", "0");
  require(unknown.validate() == HAL_EUNSUPPORTED && unknown.reports.empty(),
          "unused unsupported keyword fails explicitly");
  run cycle(R"({"$ref":"#"})", "0");
  require(cycle.validate() == HAL_EINVAL && cycle.reports.empty(),
          "non-progressing reference cycle rejected");
  run fragment(R"({"$defs":{"":false},"$ref":"#/%24defs/"})", "0");
  require(fragment.validate() == HAL_ECONFIG && fragment.reports.size() == 1 &&
              fragment.reports[0].rule == "/$defs/",
          "decoded URI fragment retains empty pointer token");
  run recursive(
      R"({"$defs":{"child":{"type":["null","object"],"properties":{"child":{"$ref":"#/$defs/child"}}}},"$ref":"#/$defs/child"})",
      R"({"child":{"child":null}})");
  require(recursive.validate() == HAL_OK,
          "recursive schema progresses through instance children");
  run limited(R"({"required":["a","b"]})", "{}");
  limited.limits.max_reports = 1;
  require(limited.validate() == HAL_EOVERFLOW && limited.reports.empty(),
          "report exhaustion publishes no prefix");
  limited.limits = defaults();
  limited.limits.max_work = 1;
  require(limited.validate() == HAL_EOVERFLOW && limited.reports.empty(),
          "work exhaustion publishes no prefix");
  limited.limits = defaults();
  limited.limits.max_pointer_bytes = 0;
  require(limited.validate() == HAL_EOVERFLOW && limited.reports.empty(),
          "pointer exhaustion publishes no truncated report");
  run regex(R"({"pattern":"(a+)+$"})",
            "\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa!\"");
  regex.limits.regex_match_limit = 10;
  require(regex.validate() == HAL_EOVERFLOW && regex.reports.empty(),
          "regex exhaustion differs from no match");
  const std::string long_subject = "\"" + std::string(1024, 'a') + "!\"";
  run restarts(R"({"pattern":"a+b"})", long_subject.c_str());
  restarts.limits.max_work = 1000000;
  require(restarts.validate() == HAL_EOVERFLOW && restarts.reports.empty(),
          "unanchored restarts charge repeated scans to the global budget");
  // PCRE2 expands this short repeat into thousands of bytecode bytes.
  run compiled(R"({"pattern":"(?:ab){200}"})", "\"\"");
  compiled.limits.max_regex_bytes = 4096;
  require(compiled.validate() == HAL_EOVERFLOW && compiled.reports.empty(),
          "compiled bytecode capacity is a limit error");
  run expansion(R"({"pattern":"(?:ab){1000}"})", "\"\"");
  expansion.limits.max_work = 10000;
  require(expansion.validate() == HAL_EOVERFLOW && expansion.reports.empty(),
          "regex allocation bytes charge compile expansion to global work");
  const auto deep_pattern = std::string("{\"pattern\":\"") +
                            std::string(40, '(') + "a" + std::string(40, ')') +
                            "\"}";
  run compile_depth(deep_pattern.c_str(), "\"a\"");
  require(compile_depth.validate() == HAL_EOVERFLOW &&
              compile_depth.reports.empty(),
          "compile recursion guard is a limit error");
  run unsupported(R"({"pattern":"\\w+"})", "\"a\"");
  require(unsupported.validate() == HAL_EUNSUPPORTED,
          "unsupported Unicode regex class is explicit");
  const char *unsupported_classes[] = {
      R"({"pattern":"^[[:digit:]]$"})",
      R"({"pattern":"^[]\\s]$"})",
      R"({"pattern":"^[^]\\s]$"})",
  };
  for (const char *schema : unsupported_classes) {
    for (const char *value : {R"("5")", R"("]")", R"(" ")", R"("\u00a0")"}) {
      run unsupported_class(schema, value);
      require(unsupported_class.validate() == HAL_EUNSUPPORTED &&
                  unsupported_class.reports.empty(),
              "unsupported character class fails before matching");
    }
  }
  run escaped_class(R"({"pattern":"^[\\]\\s]+$"})", R"("] \u00a0")");
  require(escaped_class.validate() == HAL_OK,
          "escaped closing bracket keeps whitespace inside the class");
}
hal_status_t return_status(void *user, const jh_json_schema_error_t *) {
  return *static_cast<hal_status_t *>(user);
}
void callbacks_and_workspace() {
  run bad("false", "0");
  require(bad.validate(nullptr) == HAL_ECONFIG,
          "optional report preserves invalid result");
  require(bad.validate(jh_schema_test_unknown_status) == HAL_EINTERNAL,
          "unknown numeric status from a C report callback");
  run external(R"({"$ref":"other.schema.json"})", "0");
  require(external.validate(collect, nullptr, jh_schema_test_unknown_load) ==
              HAL_EINTERNAL,
          "unknown numeric status from a C loader callback");
  for (const hal_status_t s :
       {HAL_EIO, HAL_NONE, static_cast<hal_status_t>(2)}) {
    hal_status_t code = s;
    require(bad.validate(return_status, &code) ==
                (s == HAL_EIO ? HAL_EIO : HAL_EINTERNAL),
            "callback status normalization");
  }
  size_t n = 0;
  auto limits = defaults();
  require(jh_json_schema_workspace_size(&limits, &n) == HAL_OK,
          "workspace size");
  std::vector<unsigned char> scratch(n);
  const jh_json_schema_source_t schema{{"root", 4}, &bad.schema.doc};
  require(jh_json_schema_validate(&bad.instance.doc, &schema, nullptr, nullptr,
                                  nullptr, nullptr, {scratch.data(), n - 1},
                                  &limits) == HAL_EOVERFLOW,
          "undersized workspace");
  limits.max_schema_nodes = SIZE_MAX;
  n = 123;
  require(jh_json_schema_workspace_size(&limits, &n) == HAL_EOVERFLOW &&
              n == 123,
          "checked workspace arithmetic");
}
} // namespace
int main() {
  equality_and_branches();
  references_and_limits();
  callbacks_and_workspace();
  return failures == 0 ? 0 : 1;
}
