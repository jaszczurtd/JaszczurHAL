#ifndef JH_TYPE_CATALOGUE_H
#define JH_TYPE_CATALOGUE_H

/** @file jh_type_catalogue.h
 * The catalogue of HAL component types: every binding in
 * config/hardware/bindings, validated against binding.schema.json and the
 * rules beyond the schema (C types, units, defaults, children, connections,
 * features, capabilities, cycles). Failures throw jh::failure with the
 * device tree specification's code, source file and JSON Pointer.
 */
#include "../json/jh_json_file.h"
#include <jb/literal.h>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace jh::hardware {

/** JSON kind a C type of a property holds. */
enum class c_kind { boolean, integer, floating, string };

/** One cType of binding.schema.json and how its values are checked and spelt.
 */
struct c_type_info {
  const char *name;
  c_kind category;
  bool is_signed;
  unsigned bits; /**< Integer or IEEE width; 1 for bool, 0 for string. */
  jb_c_literal_type_t literal;
};

/** The cType named by a property definition, or null for an unknown name. */
const c_type_info *c_type(const std::string &name);
/** The JSON type name a category requires in a property definition. */
const char *json_type_name(c_kind category);

/** Read-only view of one parsed value. */
struct view {
  jb_json_node_t node{};
  jb_json_type_t type() const;
  bool is(jb_json_type_t kind) const { return type() == kind; }
  /** Member by decoded name; absent members return false. */
  bool find(const char *key, view *out) const;
  view member(const char *key) const;
  std::string string() const;
  std::string key() const;
  std::string raw() const;
  bool boolean() const;
  size_t count() const;
  /** Direct members or items in document order. */
  std::vector<view> children() const;
};

/** One type: its compatible, file and parsed document. */
struct binding {
  std::string compatible;
  std::string path;
  std::string origin; /**< "hal" or "project". */
  std::unique_ptr<json_document> document;
  view root() const;
};

/** Types by compatible; load order is the sorted file order. */
class catalogue {
public:
  void add(binding entry);
  const binding *find(const std::string &compatible) const;
  const std::vector<binding> &in_load_order() const { return bindings_; }
  /** Bindings by compatible in Unicode code point order. */
  std::vector<const binding *> sorted() const;

private:
  std::vector<binding> bindings_;
  std::map<std::string, size_t> index_;
};

/** Throw the catalogue failure for code at source:pointer. */
[[noreturn]] void fail(const std::string &code, const std::string &source,
                       const std::string &pointer, const std::string &detail);

/** Flags of the HAL feature registry under root, for requiresFeatures. */
std::set<std::string> hal_features(parse_location &where,
                                   const std::string &root, size_t max_bytes);
/** Capabilities of boards/capabilities.json under root, for provides. */
std::set<std::string> board_capabilities(parse_location &where,
                                         const std::string &root,
                                         size_t max_bytes);

/** Read every *.json of bindings_dir as a HAL type named after its
 * compatible, validating each with schema_dir/binding.schema.json.
 */
catalogue load_hal_catalogue(parse_location &where,
                             const std::string &schema_dir,
                             const std::string &bindings_dir, size_t max_bytes);

/** Semantic checks of every type beyond its schema. Null feature or
 * capability sets skip the respective check.
 */
void check_catalogue(const catalogue &types,
                     const std::set<std::string> *features,
                     const std::set<std::string> *capabilities);

} // namespace jh::hardware
#endif
