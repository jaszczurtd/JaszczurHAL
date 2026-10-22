#include "jh_type_catalogue.h"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <jb/json_number.h>
#include <jb/unicode.h>
#include <jb/utf8.h>
#include <limits>
#include <utility>

namespace jh::hardware {

namespace {
const char *const binding_code = "JH-HW-BINDING";
const char *const property_code = "JH-HW-PROPERTY";
const char *const schema_code = "JH-HW-SCHEMA";

std::string index_text(size_t index) { return std::to_string(index); }

/* Python's repr of a scalar list, for the "one of [...]" message. */
std::string python_repr(view value) {
  switch (value.type()) {
  case JB_JSON_NULL:
    return "None";
  case JB_JSON_BOOL:
    return value.boolean() ? "True" : "False";
  case JB_JSON_NUMBER:
    return value.raw();
  case JB_JSON_STRING: {
    std::string text = "'";
    for (char c : value.string()) {
      if (c == '\\' || c == '\'')
        text += '\\';
      text += c;
    }
    return text + "'";
  }
  default: {
    std::string text = "[";
    bool first = true;
    for (view item : value.children()) {
      if (!first)
        text += ", ";
      first = false;
      text += python_repr(item);
    }
    return text + "]";
  }
  }
}

int number_compare(view left, view right) {
  size_t bytes;
  require(jb_json_number_compare_workspace_size(left.node, right.node, &bytes),
          4, "number comparison workspace overflow");
  std::vector<unsigned char> scratch(bytes);
  int order;
  require(jb_json_number_compare(
              left.node, right.node, {scratch.data(), scratch.size()},
              64 * (left.raw().size() + right.raw().size()) + 1000000, &order),
          4, "number comparison budget exceeded");
  return order;
}

/* JSON equality: booleans differ from numbers, 1 equals 1.0. */
bool json_equal(view left, view right) {
  const jb_json_type_t kind = left.type();
  if (kind != right.type())
    return false;
  switch (kind) {
  case JB_JSON_NULL:
    return true;
  case JB_JSON_BOOL:
    return left.boolean() == right.boolean();
  case JB_JSON_NUMBER:
    return number_compare(left, right) == 0;
  case JB_JSON_STRING:
    return left.string() == right.string();
  case JB_JSON_ARRAY: {
    const auto items = left.children(), others = right.children();
    if (items.size() != others.size())
      return false;
    for (size_t i = 0; i < items.size(); ++i)
      if (!json_equal(items[i], others[i]))
        return false;
    return true;
  }
  default: {
    if (left.count() != right.count())
      return false;
    for (view member : left.children()) {
      view other;
      if (!right.find(member.key().c_str(), &other) ||
          !json_equal(member, other))
        return false;
    }
    return true;
  }
  }
}

bool floating_fits(view value, unsigned bits) {
  size_t bytes;
  if (jb_json_float_workspace_size(value.node, bits, &bytes) != HAL_OK)
    return false;
  std::vector<unsigned char> scratch(bytes);
  const jb_json_float_options_t options{true, true,
                                        64 * value.raw().size() + 1000000};
  if (bits == 32) {
    uint32_t out;
    return jb_json_get_f32_bits(value.node, &options,
                                {scratch.data(), scratch.size()},
                                &out) == HAL_OK;
  }
  uint64_t out;
  return jb_json_get_f64_bits(value.node, &options,
                              {scratch.data(), scratch.size()}, &out) == HAL_OK;
}

/* The exact value fits the type; a float or double must round to a finite
 * value and not to zero unless it is zero. */
bool value_fits(view value, const c_type_info &type) {
  switch (type.category) {
  case c_kind::boolean:
    return value.is(JB_JSON_BOOL);
  case c_kind::string:
    return value.is(JB_JSON_STRING);
  case c_kind::integer: {
    if (!value.is(JB_JSON_NUMBER))
      return false;
    if (type.is_signed) {
      int64_t number;
      if (jb_json_get_i64(value.node, &number) != HAL_OK)
        return false;
      if (type.bits == 64)
        return true;
      const int64_t limit = int64_t{1} << (type.bits - 1);
      return number >= -limit && number < limit;
    }
    uint64_t number;
    if (jb_json_get_u64(value.node, &number) != HAL_OK)
      return false;
    return type.bits == 64 || number < (uint64_t{1} << type.bits);
  }
  case c_kind::floating:
    return value.is(JB_JSON_NUMBER) && floating_fits(value, type.bits);
  }
  return false;
}

/* Printable NFC text without surrounding whitespace. */
bool unit_valid(const std::string &unit) {
  std::vector<jb_unicode_properties_t> properties;
  for (size_t offset = 0; offset < unit.size();) {
    uint32_t scalar;
    size_t consumed;
    require(jb_utf8_decode(unit.data() + offset, unit.size() - offset, &scalar,
                           &consumed),
            2, "unit is not valid UTF-8");
    jb_unicode_properties_t property;
    require(jb_unicode_properties(scalar, &property), 2,
            "unit has an invalid scalar");
    properties.push_back(property);
    offset += consumed;
  }
  if (properties.empty())
    return true;
  if (properties.front().whitespace || properties.back().whitespace)
    return false;
  for (const auto &property : properties)
    if (!property.printable)
      return false;
  size_t elements;
  const size_t budget = unit.size() * unit.size() * 4 + 4096;
  require(jb_unicode_nfc_measure(unit.data(), unit.size(), budget, &elements),
          4, "unit normalization budget exceeded");
  std::vector<int32_t> scratch(elements);
  const char *text;
  size_t length;
  require(jb_unicode_nfc(unit.data(), unit.size(), scratch.data(),
                         scratch.size(), budget, &text, &length),
          4, "unit normalization failed");
  return length == unit.size() && std::equal(unit.begin(), unit.end(), text);
}

void check_property_value(view definition, view value,
                          const std::string &pointer,
                          const std::string &source) {
  const std::string type_name = definition.member("cType").string();
  const c_type_info *type = c_type(type_name);
  if (type == nullptr || !value_fits(value, *type))
    fail(property_code, source, pointer, "a " + type_name + " value");
  view members;
  if (definition.find("enum", &members)) {
    bool listed = false;
    for (view member : members.children())
      if (json_equal(value, member)) {
        listed = true;
        break;
      }
    if (!listed)
      fail(property_code, source, pointer, "one of " + python_repr(members));
  }
  if (!value.is(JB_JSON_NUMBER))
    return;
  view bound;
  if (definition.find("minimum", &bound) && number_compare(value, bound) < 0)
    fail(property_code, source, pointer, "at least " + bound.raw());
  if (definition.find("maximum", &bound) && number_compare(value, bound) > 0)
    fail(property_code, source, pointer, "at most " + bound.raw());
}

void check_property_definitions(const binding &entry) {
  view properties;
  if (!entry.root().find("properties", &properties))
    return;
  for (view definition : properties.children()) {
    const std::string name = definition.key();
    const std::string pointer = "/properties/" + name;
    const std::string type_name = definition.member("cType").string();
    const std::string json_type = definition.member("type").string();
    const c_type_info *type = c_type(type_name);
    if (type == nullptr || json_type_name(type->category) != json_type)
      fail(binding_code, entry.path, pointer + "/cType",
           type_name + " does not hold a JSON " + json_type);
    view value;
    if (definition.find("unit", &value) && !unit_valid(value.string()))
      fail(binding_code, entry.path, pointer + "/unit",
           "a printable unit without surrounding whitespace");
    for (const char *key : {"minimum", "maximum", "default"})
      if (definition.find(key, &value) && !value_fits(value, *type))
        fail(binding_code, entry.path, pointer + "/" + key,
             "a value that fits " + type_name);
    view members;
    if (definition.find("enum", &members)) {
      size_t index = 0;
      for (view member : members.children()) {
        if (!value_fits(member, *type))
          fail(binding_code, entry.path, pointer + "/enum/" + index_text(index),
               "a value that fits " + type_name);
        ++index;
      }
    }
    if (definition.find("default", &value))
      check_property_value(definition, value, pointer + "/default", entry.path);
  }
}

void check_signals(const binding &entry, bool has_properties, view properties) {
  view signals;
  if (!entry.root().find("signals", &signals))
    return;
  for (view signal : signals.children()) {
    view condition, ignored;
    if (!signal.find("when", &condition))
      continue;
    const std::string property = condition.member("property").string();
    if (!has_properties || !properties.find(property.c_str(), &ignored))
      fail(binding_code, entry.path,
           "/signals/" + signal.key() + "/when/property",
           "a property of this type");
  }
}

void check_children(const catalogue &types, const binding &entry) {
  view children;
  if (!entry.root().find("children", &children))
    return;
  for (view child : children.children()) {
    const std::string name = child.key();
    const std::string compatible = child.member("compatible").string();
    const binding *child_type = types.find(compatible);
    if (child_type == nullptr)
      fail(binding_code, entry.path, "/children/" + name + "/compatible",
           "unknown type " + compatible);
    view values;
    if (!child.find("properties", &values))
      continue;
    view definitions;
    const bool typed = child_type->root().find("properties", &definitions);
    for (view value : values.children()) {
      const std::string key = value.key();
      const std::string pointer = "/children/" + name + "/properties/" + key;
      view definition;
      if (!typed || !definitions.find(key.c_str(), &definition))
        fail(binding_code, entry.path, pointer, "a property of " + compatible);
      check_property_value(definition, value, pointer, entry.path);
    }
  }
}

void check_connections(const catalogue &types, const binding &entry) {
  view connections;
  if (!entry.root().find("connections", &connections))
    return;
  view signals, children, ignored;
  const bool has_signals = entry.root().find("signals", &signals);
  const bool has_children = entry.root().find("children", &children);
  std::map<std::string, std::string> owners;
  for (view connection : connections.children()) {
    const std::string name = connection.key();
    if (!has_signals || !signals.find(name.c_str(), &ignored))
      fail(binding_code, entry.path, "/connections/" + name,
           "a signal of this module");
    std::vector<std::pair<std::string, std::string>> targets;
    targets.emplace_back("/connections/" + name + "/owner",
                         connection.member("owner").string());
    size_t index = 0;
    for (view alias : connection.member("aliases").children()) {
      targets.emplace_back("/connections/" + name + "/aliases/" +
                               index_text(index),
                           alias.string());
      ++index;
    }
    for (const auto &[pointer, target] : targets) {
      const size_t dot = target.find('.');
      const std::string child = target.substr(0, dot);
      const std::string signal =
          dot == std::string::npos ? std::string() : target.substr(dot + 1);
      const binding *child_type = nullptr;
      view declared;
      if (has_children && children.find(child.c_str(), &declared))
        child_type = types.find(declared.member("compatible").string());
      view child_signals;
      if (child_type == nullptr ||
          !child_type->root().find("signals", &child_signals) ||
          !child_signals.find(signal.c_str(), &ignored))
        fail(binding_code, entry.path, pointer, "a signal of a declared child");
      const auto [owner, inserted] = owners.emplace(target, name);
      if (!inserted)
        fail(binding_code, entry.path, pointer,
             "a signal not already connected through " + owner->second);
    }
  }
}

void check_listed(const binding &entry, const char *field,
                  const std::set<std::string> &allowed, const char *expected) {
  view listed;
  if (!entry.root().find(field, &listed))
    return;
  size_t index = 0;
  for (view item : listed.children()) {
    if (allowed.count(item.string()) == 0)
      fail(binding_code, entry.path,
           std::string("/") + field + "/" + index_text(index), expected);
    ++index;
  }
}

std::vector<std::string> child_compatibles(const binding &entry) {
  std::vector<std::string> result;
  view children;
  if (entry.root().find("children", &children))
    for (view child : children.children())
      result.push_back(child.member("compatible").string());
  return result;
}

/* Depth-first over child types, in compatible order, with an explicit stack;
 * a type reached while still open is a cycle reported at the search root. */
void check_cycles(const catalogue &types) {
  std::map<std::string, int> state;
  struct frame {
    const binding *entry;
    std::vector<std::string> kids;
    size_t next;
  };
  for (const binding *root : types.sorted()) {
    if (state[root->compatible] == 2)
      continue;
    std::vector<frame> stack;
    const auto open = [&](const binding *entry) {
      state[entry->compatible] = 1;
      stack.push_back({entry, child_compatibles(*entry), 0});
    };
    open(root);
    while (!stack.empty()) {
      frame &top = stack.back();
      if (top.next == top.kids.size()) {
        state[top.entry->compatible] = 2;
        stack.pop_back();
        continue;
      }
      const std::string kid = top.kids[top.next++];
      const binding *kid_type = types.find(kid);
      if (kid_type == nullptr || state[kid] == 2)
        continue;
      if (state[kid] == 1) {
        std::string chain;
        for (const frame &open_frame : stack)
          chain += open_frame.entry->compatible + " -> ";
        fail(binding_code, stack.front().entry->path, "/children",
             "cyclic expansion: " + chain + kid);
      }
      open(kid_type);
    }
  }
}

std::vector<std::string> json_files(const std::string &directory) {
  namespace fs = std::filesystem;
  std::vector<std::string> names;
  try {
    for (const auto &entry : fs::directory_iterator(fs::u8path(directory))) {
      const std::string name = entry.path().filename().u8string();
      if (name.size() > 5 && name.compare(name.size() - 5, 5, ".json") == 0 &&
          entry.is_regular_file())
        names.push_back(name);
    }
  } catch (const fs::filesystem_error &error) {
    throw failure{
        HAL_ENOENT, 3,  "cannot list " + directory + ": " + error.what(),
        {},         {}, {}};
  }
  std::sort(names.begin(), names.end());
  std::vector<std::string> paths;
  for (const auto &name : names)
    paths.push_back(absolute_path(name, directory));
  return paths;
}

std::string file_name(const std::string &path) {
  return std::filesystem::u8path(path).filename().u8string();
}

binding read_binding(parse_location &where, schema_files &schemas,
                     std::vector<unsigned char> &scratch,
                     const std::string &path, const char *origin) {
  std::unique_ptr<json_document> document;
  try {
    document = read_document(where, path, schemas.max_bytes, true);
  } catch (const failure &error) {
    if (error.exit_code != 2)
      throw;
    fail(schema_code, path, "", error.message);
  }
  if (validate_with(schemas, *document, scratch) != HAL_OK) {
    if (schemas.reports.empty())
      fail(schema_code, path, "", "the binding schema rejects this file");
    const auto &first = schemas.reports.front();
    fail(schema_code, path, first.pointer, first.message);
  }
  binding entry;
  entry.path = path;
  entry.origin = origin;
  entry.document = std::move(document);
  entry.compatible = entry.root().member("compatible").string();
  return entry;
}

std::set<std::string> object_keys(parse_location &where,
                                  const std::string &path, size_t max_bytes,
                                  const char *field) {
  const auto document = read_document(where, path, max_bytes);
  jb_json_node_t root;
  require(jb_json_root(&document->doc, &root), 2, "document has no root");
  view table;
  if (!view{root}.is(JB_JSON_OBJECT) || !view{root}.find(field, &table) ||
      !table.is(JB_JSON_OBJECT))
    throw failure{HAL_ECONFIG,
                  2,
                  path + ": expected an object with a " + field + " object",
                  {},
                  {},
                  {}};
  std::set<std::string> keys;
  for (view member : table.children())
    keys.insert(member.key());
  return keys;
}
} // namespace

const c_type_info *c_type(const std::string &name) {
  static const c_type_info types[] = {
      {"bool", c_kind::boolean, false, 1, JB_C_BOOL},
      {"uint8", c_kind::integer, false, 8, JB_C_U8},
      {"uint16", c_kind::integer, false, 16, JB_C_U16},
      {"uint32", c_kind::integer, false, 32, JB_C_U32},
      {"uint64", c_kind::integer, false, 64, JB_C_U64},
      {"int8", c_kind::integer, true, 8, JB_C_I8},
      {"int16", c_kind::integer, true, 16, JB_C_I16},
      {"int32", c_kind::integer, true, 32, JB_C_I32},
      {"int64", c_kind::integer, true, 64, JB_C_I64},
      {"float", c_kind::floating, true, 32, JB_C_F32},
      {"double", c_kind::floating, true, 64, JB_C_F64},
      {"string", c_kind::string, false, 0, JB_C_STRING},
  };
  for (const auto &type : types)
    if (name == type.name)
      return &type;
  return nullptr;
}

const char *json_type_name(c_kind category) {
  switch (category) {
  case c_kind::boolean:
    return "boolean";
  case c_kind::integer:
    return "integer";
  case c_kind::floating:
    return "number";
  case c_kind::string:
    return "string";
  }
  return "";
}

jb_json_type_t view::type() const {
  jb_json_type_t kind;
  require(jb_json_type(node, &kind), 3, "cannot read a JSON node");
  return kind;
}

bool view::find(const char *name, view *out) const {
  jb_json_node_t found;
  const hal_status_t status = jb_json_find(node, span(name), &found);
  if (status == HAL_ENOENT)
    return false;
  require(status, 3, "cannot search a JSON object");
  out->node = found;
  return true;
}

view view::member(const char *name) const {
  view found;
  if (!find(name, &found))
    throw failure{HAL_ENOENT,
                  3,
                  std::string("the schema-validated document lacks ") + name,
                  {},
                  {},
                  {}};
  return found;
}

std::string view::string() const {
  size_t needed;
  require(jb_json_string_copy(node, nullptr, 0, &needed), 3,
          "cannot read a JSON string");
  std::string text(needed, '\0');
  require(jb_json_string_copy(node, text.data(), text.size(), &needed), 3,
          "cannot read a JSON string");
  return text;
}

std::string view::key() const {
  size_t needed;
  require(jb_json_key_copy(node, nullptr, 0, &needed), 3,
          "cannot read a JSON member name");
  std::string text(needed, '\0');
  require(jb_json_key_copy(node, text.data(), text.size(), &needed), 3,
          "cannot read a JSON member name");
  return text;
}

std::string view::raw() const {
  jb_json_span_t bytes;
  require(jb_json_raw(node, &bytes), 3, "cannot read a JSON value");
  return copied(bytes);
}

bool view::boolean() const {
  bool value;
  require(jb_json_get_bool(node, &value), 3, "cannot read a JSON boolean");
  return value;
}

size_t view::count() const {
  size_t value;
  require(jb_json_count(node, &value), 3, "cannot count a JSON container");
  return value;
}

std::vector<view> view::children() const {
  std::vector<view> result;
  jb_json_node_t child;
  hal_status_t status = jb_json_first(node, &child);
  while (status == HAL_OK) {
    result.push_back(view{child});
    status = jb_json_next(child, &child);
  }
  if (status != HAL_ENOENT)
    require(status, 3, "cannot iterate a JSON container");
  return result;
}

view binding::root() const {
  jb_json_node_t node;
  require(jb_json_root(&document->doc, &node), 3, "binding has no root");
  return view{node};
}

void catalogue::add(binding entry) {
  const auto existing = index_.find(entry.compatible);
  if (existing != index_.end())
    fail(binding_code, entry.path, "/compatible",
         entry.compatible + " is already defined by " +
             bindings_[existing->second].path);
  index_.emplace(entry.compatible, bindings_.size());
  bindings_.push_back(std::move(entry));
}

const binding *catalogue::find(const std::string &compatible) const {
  const auto found = index_.find(compatible);
  return found == index_.end() ? nullptr : &bindings_[found->second];
}

std::vector<const binding *> catalogue::sorted() const {
  std::vector<const binding *> result;
  for (const auto &[compatible, position] : index_)
    result.push_back(&bindings_[position]);
  return result;
}

void fail(const std::string &code, const std::string &source,
          const std::string &pointer, const std::string &detail) {
  throw failure{HAL_ECONFIG,
                2,
                "[" + code + "] " + source + ":" +
                    (pointer.empty() ? "/" : pointer) + ": " + detail,
                code,
                source,
                pointer};
}

std::set<std::string> hal_features(parse_location &where,
                                   const std::string &root, size_t max_bytes) {
  std::set<std::string> features;
  for (const auto &path : json_files(absolute_path("config/features", root))) {
    const auto symbols = object_keys(where, path, max_bytes, "symbols");
    features.insert(symbols.begin(), symbols.end());
  }
  return features;
}

std::set<std::string> board_capabilities(parse_location &where,
                                         const std::string &root,
                                         size_t max_bytes) {
  return object_keys(where, absolute_path("boards/capabilities.json", root),
                     max_bytes, "capabilities");
}

catalogue load_hal_catalogue(parse_location &where,
                             const std::string &schema_dir,
                             const std::string &bindings_dir,
                             size_t max_bytes) {
  schema_files schemas(where, max_bytes);
  schemas.root(absolute_path("binding.schema.json", schema_dir));
  std::vector<unsigned char> scratch;
  catalogue result;
  for (const auto &path : json_files(bindings_dir)) {
    binding entry = read_binding(where, schemas, scratch, path, "hal");
    std::string expected = entry.compatible;
    std::replace(expected.begin(), expected.end(), ',', '-');
    expected += ".json";
    if (file_name(path) != expected)
      fail(binding_code, path, "/compatible",
           "HAL type file must be named " + expected);
    result.add(std::move(entry));
  }
  return result;
}

void check_catalogue(const catalogue &types,
                     const std::set<std::string> *features,
                     const std::set<std::string> *capabilities) {
  for (const binding &entry : types.in_load_order()) {
    check_property_definitions(entry);
    view properties;
    const bool has_properties = entry.root().find("properties", &properties);
    check_signals(entry, has_properties, properties);
    check_children(types, entry);
    check_connections(types, entry);
    if (features != nullptr)
      check_listed(entry, "requiresFeatures", *features,
                   "a flag of the HAL feature registry");
    if (capabilities != nullptr)
      check_listed(entry, "provides", *capabilities,
                   "a capability of boards/capabilities.json");
  }
  check_cycles(types);
}

} // namespace jh::hardware
