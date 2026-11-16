/* Compile jb_c_literal output as both C and C++; compare the resulting
 * bytes with the independent expectations of the former Python probe. */
#include <jb/array.h>
#include <jb/literal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const struct {
  const char *json, *bytes;
} strings[] = {
    {"\"µA\"", "194, 181, 65"},
    {"\"\\u00001\"", "0, 49"},
    {"\"\\u00019\"", "1, 57"},
    {"\"x\\u007f7\"", "120, 127, 55"},
    {"\"a?"
     "?=b?"
     "?/?\"",
     "97, 63, 63, 61, 98, 63, 63, 47, 63"},
    {"\"q\\\"\\\\\"", "113, 34, 92"},
    {"\"ż€😀F\"", "197, 188, 226, 130, 172, 240, 159, 152, 128, 70"},
};

static const struct {
  jb_c_literal_type_t type;
  const char *token, *bits;
} numbers[] = {
    {JB_C_F32, "0.22", "3e6147ae"},
    {JB_C_F32, "-0.22", "be6147ae"},
    {JB_C_F32, "1.0000000596046447753906250000000000000000001", "3f800001"},
    {JB_C_F32, "1e-45", "00000001"},
    {JB_C_F32, "3.4028235e38", "7f7fffff"},
    {JB_C_F64, "0.22", "3fcc28f5c28f5c29"},
    {JB_C_F64, "1", "3ff0000000000000"},
    {JB_C_F64, "4e-324", "0000000000000001"},
    {JB_C_F64, "1.7976931348623157e308", "7fefffffffffffff"},
};

static hal_status_t literal(const char *json, jb_c_literal_type_t type,
                            char *out, size_t capacity) {
  const jb_json_limits_t limits = {256, 1, 1, 256, 256, 1, 256};
  size_t bytes = 0, count = 0;
  hal_status_t status = jb_json_parse_workspace_size(&limits, &bytes);
  void *parse = status == HAL_OK ? malloc(bytes) : NULL;
  if (status == HAL_OK && parse == NULL)
    status = HAL_ENOMEM;
  jb_json_token_t token = {0};
  jb_json_doc_t document = {0};
  jb_json_node_t node = {0};
  if (status == HAL_OK)
    status =
        jb_json_parse((jb_json_span_t){json, strlen(json)}, &limits, &token, 1,
                      (jb_json_workspace_t){parse, bytes}, &document, NULL);
  if (status == HAL_OK)
    status = jb_json_root(&document, &node);
  if (status == HAL_OK)
    status = jb_c_literal_workspace_size(node, type, &bytes);
  void *scratch = status == HAL_OK && bytes != 0 ? malloc(bytes) : NULL;
  if (status == HAL_OK && bytes != 0 && scratch == NULL)
    status = HAL_ENOMEM;
  if (status == HAL_OK)
    status = jb_c_literal(node, type, (jb_json_workspace_t){scratch, bytes},
                          1000000, out, capacity - 1, &count);
  if (status == HAL_OK)
    out[count] = '\0';
  free(scratch);
  free(parse);
  return status;
}

int main(void) {
  char output[256];
  (void)puts("#include <stdint.h>\n#include <stdio.h>\n#include <string.h>\n");
  for (size_t i = 0; i < COUNTOF(strings); ++i) {
    if (literal(strings[i].json, JB_C_STRING, output, sizeof output) != HAL_OK)
      return 1;
    (void)printf("static const char literal%zu[] = %s;\n"
                 "static const unsigned char bytes%zu[] = {%s};\n",
                 i, output, i, strings[i].bytes);
  }
  for (size_t i = 0; i < COUNTOF(numbers); ++i) {
    if (literal(numbers[i].token, numbers[i].type, output, sizeof output) !=
        HAL_OK)
      return 1;
    (void)printf("static const %s number%zu = %s;\n"
                 "static const uint64_t bits%zu = UINT64_C(0x%s);\n",
                 numbers[i].type == JB_C_F32 ? "float" : "double", i, output, i,
                 numbers[i].bits);
  }
  (void)puts(
      "static int differs(unsigned index, const char *text, size_t size,\n"
      "                   const unsigned char *bytes, size_t count) {\n"
      "  if (size == count + 1u && memcmp(text, bytes, count) == 0) return 0;\n"
      "  printf(\"literal %u differs from its UTF-8 bytes\\n\", index);\n"
      "  return 1;\n}\n"
      "static int bits_differ(unsigned index, const void *number, size_t size, "
      "uint64_t expected) {\n"
      "  uint32_t narrow = 0;\n  uint64_t wide = 0;\n"
      "  if (size == sizeof narrow) {\n"
      "    memcpy(&narrow, number, size);\n    wide = narrow;\n"
      "  } else {\n    memcpy(&wide, number, size);\n  }\n"
      "  if (wide == expected) return 0;\n"
      "  printf(\"number %u differs from its bit pattern\\n\", index);\n"
      "  return 1;\n}\n"
      "int main(void) {\n  int failures = 0;");
  for (size_t i = 0; i < COUNTOF(strings); ++i)
    (void)printf("  failures += differs(%zuu, literal%zu, sizeof literal%zu, "
                 "bytes%zu, sizeof bytes%zu);\n",
                 i, i, i, i, i);
  for (size_t i = 0; i < COUNTOF(numbers); ++i)
    (void)printf("  failures += bits_differ(%zuu, &number%zu, sizeof "
                 "number%zu, bits%zu);\n",
                 i, i, i, i);
  (void)puts("  return failures;\n}");
  return fflush(stdout) == 0 && !ferror(stdout) ? 0 : 1;
}
