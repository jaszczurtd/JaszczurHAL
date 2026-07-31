# cJSON integration

The upstream cJSON checkout is managed at `third_party/cJSON` from the exact
commit in `third_party/cjson_version.conf`. The files in this directory are
feature-gating wrappers that preserve the existing JaszczurHAL include paths.

Synchronize or verify the dependency with `scripts/ensure_cjson.sh`.
