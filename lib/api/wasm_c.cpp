// SPDX-License-Identifier: Apache-2.0
#include <algorithm>

#include "common/configure.h"
#include "common/errcode.h"
#include "common/log.h"
#include "common/span.h"
#include "common/value.h"

#include "aot/compiler.h"
#include "ast/module.h"
#include "interpreter/interpreter.h"
#include "loader/loader.h"
#include "runtime/storemgr.h"
#include "validator/validator.h"

#include "api/wasmedge.h"
#include "wasm/wasm.h"

#ifndef own
#define own
#endif

/// wasm_config_t implementation.
struct wasm_config_t {
  void destroy() { delete this; }
  WasmEdge::Configure conf;
};

/// wasm_engine_t implementation.
struct wasm_engine_t {
  wasm_engine_t() noexcept : conf(), interp(conf, nullptr) {}
  wasm_engine_t(const wasm_config_t *c) noexcept
      : conf(c->conf), interp(c->conf, nullptr) {}
  void destroy() { delete this; }
  WasmEdge::Configure conf;
  WasmEdge::Interpreter::Interpreter interp;
};

/// wasm_store_t implementation.
struct wasm_store_t {
  wasm_store_t(wasm_engine_t *e) noexcept
      : engine(e), load(e->conf), valid(e->conf), store() {}
  void destroy() { delete this; }
  wasm_engine_t *engine;
  WasmEdge::Loader::Loader load;
  WasmEdge::Validator::Validator valid;
  WasmEdge::Runtime::StoreManager store;
};

/// wasm_valtype_t implementation.
struct wasm_valtype_t {
  wasm_valtype_t(const wasm_valtype_t *vt) noexcept : kind(vt->kind) {}
  wasm_valtype_t(wasm_valkind_t k) noexcept : kind(k) {}
  own wasm_valtype_t *copy() const {
    return new (std::nothrow) wasm_valtype_t(this);
  }
  void destroy() { delete this; }
  wasm_valkind_t kind;
};

/// wasm_externtype_t implementation.
struct wasm_externtype_t {
  wasm_externtype_t(wasm_externkind_t k) noexcept : kind(k) {}
  own wasm_externtype_t *copy() const;
  void destroy();
  wasm_externkind_t kind;
};

/// wasm_functype_t implementation.
struct wasm_functype_t : wasm_externtype_t {
  wasm_functype_t(const wasm_functype_t *functype)
      : wasm_externtype_t(WASM_EXTERN_FUNC) {
    wasm_valtype_vec_copy(&params, &(functype->params));
    wasm_valtype_vec_copy(&results, &(functype->results));
  }
  wasm_functype_t(own wasm_valtype_vec_t *vp,
                  own wasm_valtype_vec_t *vr) noexcept
      : wasm_externtype_t(WASM_EXTERN_FUNC) {
    std::swap(params, *vp);
    std::swap(results, *vr);
  }
  ~wasm_functype_t() noexcept {
    wasm_valtype_vec_delete(&params);
    wasm_valtype_vec_delete(&results);
  }
  own wasm_functype_t *copy() const {
    return new (std::nothrow) wasm_functype_t(this);
  }
  void destroy() { delete this; }
  wasm_valtype_vec_t params, results;
};

/// wasm_globaltype_t implementation.
struct wasm_globaltype_t : wasm_externtype_t {
  wasm_globaltype_t(const wasm_globaltype_t *globaltype) noexcept
      : wasm_externtype_t(WASM_EXTERN_GLOBAL),
        content(globaltype->content.kind), mutability(globaltype->mutability) {}
  wasm_globaltype_t(own wasm_valtype_t *vt,
                    const wasm_mutability_t mut) noexcept
      : wasm_externtype_t(WASM_EXTERN_GLOBAL), content(vt), mutability(mut) {
    wasm_valtype_delete(vt);
  }
  own wasm_globaltype_t *copy() const {
    return new (std::nothrow) wasm_globaltype_t(this);
  }
  void destroy() { delete this; }
  wasm_valtype_t content;
  wasm_mutability_t mutability;
};

/// wasm_tabletype_t implementation.
struct wasm_tabletype_t : wasm_externtype_t {
  wasm_tabletype_t(const wasm_tabletype_t *tabletype) noexcept
      : wasm_externtype_t(WASM_EXTERN_TABLE), valtype(&(tabletype->valtype)),
        limits(tabletype->limits) {}
  wasm_tabletype_t(own wasm_valtype_t *vt, const wasm_limits_t *lim) noexcept
      : wasm_externtype_t(WASM_EXTERN_TABLE), valtype(vt), limits(*lim) {
    wasm_valtype_delete(vt);
  }
  own wasm_tabletype_t *copy() const {
    return new (std::nothrow) wasm_tabletype_t(this);
  }
  void destroy() { delete this; }
  wasm_valtype_t valtype;
  wasm_limits_t limits;
};

/// wasm_memorytype_t implementation.
struct wasm_memorytype_t : wasm_externtype_t {
  wasm_memorytype_t(const wasm_memorytype_t *memorytype) noexcept
      : wasm_externtype_t(WASM_EXTERN_MEMORY), limits(memorytype->limits) {}
  wasm_memorytype_t(const wasm_limits_t *lim) noexcept
      : wasm_externtype_t(WASM_EXTERN_MEMORY), limits(*lim) {}
  own wasm_memorytype_t *copy() const {
    return new (std::nothrow) wasm_memorytype_t(this);
  }
  void destroy() { delete this; }
  wasm_limits_t limits;
};

/// wasm_importtype_t implementation.
struct wasm_importtype_t {
  wasm_importtype_t(const wasm_importtype_t *importtype) {
    wasm_name_copy(&modname, &(importtype->modname));
    wasm_name_copy(&name, &(importtype->name));
    type = wasm_externtype_copy(importtype->type);
  }
  wasm_importtype_t(own wasm_name_t *mod, own wasm_name_t *n,
                    own wasm_externtype_t *externtype) noexcept {
    std::swap(modname, *mod);
    std::swap(name, *n);
    type = externtype;
  }
  ~wasm_importtype_t() noexcept {
    wasm_name_delete(&modname);
    wasm_name_delete(&name);
    wasm_externtype_delete(type);
  }
  own wasm_importtype_t *copy() const {
    return new (std::nothrow) wasm_importtype_t(this);
  }
  void destroy() { delete this; }
  wasm_name_t modname, name;
  wasm_externtype_t *type;
};

/// wasm_exporttype_t implementation.
struct wasm_exporttype_t {
  wasm_exporttype_t(const wasm_exporttype_t *exporttype) {
    wasm_name_copy(&name, &(exporttype->name));
    type = wasm_externtype_copy(exporttype->type);
  }
  wasm_exporttype_t(own wasm_name_t *n,
                    own wasm_externtype_t *externtype) noexcept {
    std::swap(name, *n);
    type = externtype;
  }
  ~wasm_exporttype_t() noexcept {
    wasm_name_delete(&name);
    wasm_externtype_delete(type);
  }
  own wasm_exporttype_t *copy() const {
    return new (std::nothrow) wasm_exporttype_t(this);
  }
  void destroy() { delete this; }
  wasm_name_t name;
  wasm_externtype_t *type;
};

own wasm_externtype_t *wasm_externtype_t::copy() const {
  switch (kind) {
  case WASM_EXTERN_FUNC:
    return new (std::nothrow)
        wasm_functype_t(wasm_externtype_as_functype_const(this));
  case WASM_EXTERN_GLOBAL:
    return new (std::nothrow)
        wasm_globaltype_t(wasm_externtype_as_globaltype_const(this));
  case WASM_EXTERN_TABLE:
    return new (std::nothrow)
        wasm_tabletype_t(wasm_externtype_as_tabletype_const(this));
  case WASM_EXTERN_MEMORY:
    return new (std::nothrow)
        wasm_memorytype_t(wasm_externtype_as_memorytype_const(this));
  default:
    __builtin_unreachable();
  }
}

void wasm_externtype_t::destroy() {
  switch (kind) {
  case WASM_EXTERN_FUNC:
    delete wasm_externtype_as_functype(this);
    return;
  case WASM_EXTERN_GLOBAL:
    delete wasm_externtype_as_globaltype(this);
    return;
  case WASM_EXTERN_TABLE:
    delete wasm_externtype_as_tabletype(this);
    return;
  case WASM_EXTERN_MEMORY:
    delete wasm_externtype_as_memorytype(this);
    return;
  default:
    __builtin_unreachable();
  }
}

/// wasm_ref_t implementation.
struct wasm_ref_t {
  wasm_ref_t(const wasm_ref_t *ref) noexcept
      : host_info(ref->host_info), finalizer(ref->finalizer) {}
  wasm_ref_t(void *host, void (*fin)(void *)) noexcept
      : host_info(host), finalizer(fin) {}
  virtual ~wasm_ref_t() {}
  virtual bool same(const wasm_ref_t *lhs) const {
    return lhs && lhs->host_info == host_info && lhs->finalizer == finalizer;
  }
  virtual own wasm_ref_t *copy() const {
    return new (std::nothrow) wasm_ref_t(this);
  }
  virtual void destroy() { delete this; }
  void *host_info;
  void (*finalizer)(void *);
};

namespace {

/// Helper macro for getting attributes.
#define WASM_STRUCT_GET_ATTR(name, attr, default, ref_or_none)                 \
  if (name) {                                                                  \
    return ref_or_none(name->attr);                                            \
  }                                                                            \
  return default

/// Helper macro for the WASM_DECLARE_OWN implementation.
#define WASM_DECLARE_OWN_IMPL(name)                                            \
  WASMEDGE_CAPI_EXPORT void wasm_##name##_delete(own wasm_##name##_t *obj) {   \
    WASM_STRUCT_GET_ATTR(obj, destroy(), , );                                  \
  }

/// Helper macro for the copy function implementation.
#define WASM_DECLARE_COPY_IMPL(name)                                           \
  WASMEDGE_CAPI_EXPORT own wasm_##name##_t *wasm_##name##_copy(                \
      const wasm_##name##_t *in) {                                             \
    WASM_STRUCT_GET_ATTR(in, copy(), nullptr, );                               \
  }

/// Helper macro for the parts of WASM_DECLARE_VEC implementation.
#define WASM_DECLARE_VEC_COMMON_IMPL(name, ptr_or_none)                        \
  WASMEDGE_CAPI_EXPORT void wasm_##name##_vec_new_empty(                       \
      own wasm_##name##_vec_t *out) {                                          \
    if (out) {                                                                 \
      out->size = 0;                                                           \
      out->data = nullptr;                                                     \
    }                                                                          \
  }                                                                            \
  WASMEDGE_CAPI_EXPORT void wasm_##name##_vec_new_uninitialized(               \
      own wasm_##name##_vec_t *out, size_t size) {                             \
    if (out) {                                                                 \
      out->size = size;                                                        \
      out->data = nullptr;                                                     \
      if (size > 0) {                                                          \
        out->data = new (std::nothrow) wasm_##name##_t ptr_or_none[size];      \
        std::memset(out->data, 0, sizeof(wasm_##name##_t ptr_or_none) * size); \
      }                                                                        \
    }                                                                          \
  }                                                                            \
  WASMEDGE_CAPI_EXPORT void wasm_##name##_vec_new(                             \
      own wasm_##name##_vec_t *out, size_t size,                               \
      own wasm_##name##_t ptr_or_none const vals[]) {                          \
    if (out) {                                                                 \
      out->size = size;                                                        \
      out->data = nullptr;                                                     \
      if (size > 0) {                                                          \
        out->data = new (std::nothrow) wasm_##name##_t ptr_or_none[size];      \
        std::copy_n(std::make_move_iterator(vals), size, out->data);           \
      }                                                                        \
    }                                                                          \
  }

/// Helper macro for the WASM_DECLARE_VEC with pointer implementation.
#define WASM_DECLARE_VEC_PTR_IMPL(name)                                        \
  WASM_DECLARE_VEC_COMMON_IMPL(name, *)                                        \
  WASMEDGE_CAPI_EXPORT void wasm_##name##_vec_copy(                            \
      own wasm_##name##_vec_t *out, const wasm_##name##_vec_t *in) {           \
    if (in && out) {                                                           \
      out->size = in->size;                                                    \
      out->data = nullptr;                                                     \
      if (in->size > 0) {                                                      \
        out->data = new (std::nothrow) wasm_##name##_t *[in->size];            \
        for (size_t i = 0; i < in->size; i++) {                                \
          out->data[i] = wasm_##name##_copy(in->data[i]);                      \
        }                                                                      \
      }                                                                        \
    }                                                                          \
  }                                                                            \
  WASMEDGE_CAPI_EXPORT void wasm_##name##_vec_delete(                          \
      own wasm_##name##_vec_t *vec) {                                          \
    if (vec) {                                                                 \
      if (vec->size > 0) {                                                     \
        for (size_t i = 0; i < vec->size; i++) {                               \
          wasm_##name##_delete(vec->data[i]);                                  \
        }                                                                      \
        delete[] vec->data;                                                    \
      }                                                                        \
      vec->size = 0;                                                           \
      vec->data = nullptr;                                                     \
    }                                                                          \
  }

/// Helper macro for the WASM_DECLARE_VEC with scalar implementation.
#define WASM_DECLARE_VEC_SCALAR_IMPL(name)                                     \
  WASM_DECLARE_VEC_COMMON_IMPL(name, )                                         \
  WASMEDGE_CAPI_EXPORT void wasm_##name##_vec_copy(                            \
      own wasm_##name##_vec_t *out, const wasm_##name##_vec_t *in) {           \
    if (in && out) {                                                           \
      out->size = in->size;                                                    \
      if (in->size > 0) {                                                      \
        out->data = new (std::nothrow) wasm_##name##_t[in->size];              \
        std::copy_n(in->data, in->size, out->data);                            \
      }                                                                        \
    }                                                                          \
  }                                                                            \
  WASMEDGE_CAPI_EXPORT void wasm_##name##_vec_delete(                          \
      own wasm_##name##_vec_t *vec) {                                          \
    if (vec) {                                                                 \
      if (vec->size > 0) {                                                     \
        delete[] vec->data;                                                    \
      }                                                                        \
      vec->size = 0;                                                           \
      vec->data = nullptr;                                                     \
    }                                                                          \
  }

/// Helper macro for the WASM_DECLARE_TYPE implementation.
#define WASM_DECLARE_TYPE_IMPL(name)                                           \
  WASM_DECLARE_OWN_IMPL(name)                                                  \
  WASM_DECLARE_VEC_PTR_IMPL(name)                                              \
  WASM_DECLARE_COPY_IMPL(name)

/// Helper macro for the WASM_DECLARE_REF_BASE implementation.
#define WASM_DECLARE_REF_BASE_IMPL(name)                                       \
  WASM_DECLARE_OWN_IMPL(name)                                                  \
  WASM_DECLARE_COPY_IMPL(name)                                                 \
  WASMEDGE_CAPI_EXPORT bool wasm_##name##_same(const wasm_##name##_t *inst1,   \
                                               const wasm_##name##_t *inst2) { \
    return inst1 && inst1->same(inst2);                                        \
  }                                                                            \
                                                                               \
  WASMEDGE_CAPI_EXPORT void *wasm_##name##_get_host_info(                      \
      const wasm_##name##_t *inst) {                                           \
    WASM_STRUCT_GET_ATTR(inst, host_info, nullptr, );                          \
  }                                                                            \
  WASMEDGE_CAPI_EXPORT void wasm_##name##_set_host_info(wasm_##name##_t *inst, \
                                                        void *info) {          \
    if (inst) {                                                                \
      inst->host_info = info;                                                  \
    }                                                                          \
  }                                                                            \
  WASMEDGE_CAPI_EXPORT void wasm_##name##_set_host_info_with_finalizer(        \
      wasm_##name##_t *inst, void *info, void (*fin)(void *)) {                \
    if (inst) {                                                                \
      inst->host_info = info;                                                  \
      inst->finalizer = fin;                                                   \
    }                                                                          \
  }

/// Helper macro for the WASM_DECLARE_REF implementation.
#define WASM_DECLARE_REF_IMPL(name)                                            \
  WASM_DECLARE_REF_BASE_IMPL(name)                                             \
                                                                               \
  WASMEDGE_CAPI_EXPORT wasm_ref_t *wasm_##name##_as_ref(                       \
      wasm_##name##_t *inst) {                                                 \
    return inst;                                                               \
  }                                                                            \
  WASMEDGE_CAPI_EXPORT wasm_##name##_t *wasm_ref_as_##name(wasm_ref_t *ref) {  \
    return static_cast<wasm_##name##_t *>(ref);                                \
  }                                                                            \
  WASMEDGE_CAPI_EXPORT const wasm_ref_t *wasm_##name##_as_ref_const(           \
      const wasm_##name##_t *inst) {                                           \
    return inst;                                                               \
  }                                                                            \
  WASMEDGE_CAPI_EXPORT const wasm_##name##_t *wasm_ref_as_##name##_const(      \
      const wasm_ref_t *ref) {                                                 \
    return static_cast<const wasm_##name##_t *>(ref);                          \
  }

/*
/// Helper macro for the WASM_DECLARE_SHARABLE_REF implementation.
#define WASM_DECLARE_SHARABLE_REF_IMPL(name)                                   \
  WASM_DECLARE_REF_IMPL(name)                                                  \
  WASM_DECLARE_OWN_IMPL(shared_##name)                                         \
                                                                               \
  WASMEDGE_CAPI_EXPORT own wasm_shared_##name##_t *wasm_##name##_share(        \
      const wasm_##name##_t *);                                                \
  WASMEDGE_CAPI_EXPORT own wasm_##name##_t *wasm_##name##_obtain(              \
      wasm_store_t *, const wasm_shared_##name##_t *);
*/

} // namespace

#ifdef __cplusplus
extern "C" {
#endif

/// >>>>>>>> wasm_byte_vec_t functions >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

WASM_DECLARE_VEC_SCALAR_IMPL(byte)

/// <<<<<<<< wasm_byte_vec_t functions <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

/// >>>>>>>> wasm_config_t functions >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

WASM_DECLARE_OWN_IMPL(config)

WASMEDGE_CAPI_EXPORT own wasm_config_t *wasm_config_new() {
  return new (std::nothrow) wasm_config_t;
}

/// <<<<<<<< wasm_config_t functions <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

/// >>>>>>>> wasm_engine_t functions >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

WASM_DECLARE_OWN_IMPL(engine)

WASMEDGE_CAPI_EXPORT own wasm_engine_t *wasm_engine_new() {
  return new (std::nothrow) wasm_engine_t();
}

WASMEDGE_CAPI_EXPORT own wasm_engine_t *
wasm_engine_new_with_config(own wasm_config_t *conf) {
  wasm_engine_t *res = new (std::nothrow) wasm_engine_t(conf);
  wasm_config_delete(conf);
  return res;
}

/// <<<<<<<< wasm_store_t functions <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

WASM_DECLARE_OWN_IMPL(store)

WASMEDGE_CAPI_EXPORT own wasm_store_t *wasm_store_new(wasm_engine_t *engine) {
  if (engine) {
    return new (std::nothrow) wasm_store_t(engine);
  }
  return nullptr;
}

/// >>>>>>>> wasm_store_t functions >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

/// <<<<<<<< wasm_valtype_t functions <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

WASM_DECLARE_TYPE_IMPL(valtype)

WASMEDGE_CAPI_EXPORT own wasm_valtype_t *wasm_valtype_new(wasm_valkind_t kind) {
  return new (std::nothrow) wasm_valtype_t(kind);
}

WASMEDGE_CAPI_EXPORT wasm_valkind_t
wasm_valtype_kind(const wasm_valtype_t *valtype) {
  WASM_STRUCT_GET_ATTR(valtype, kind, WASM_I32, );
}

/// >>>>>>>> wasm_valtype_t functions >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

/// <<<<<<<< wasm_functype_t functions <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

WASM_DECLARE_TYPE_IMPL(functype)

WASMEDGE_CAPI_EXPORT own wasm_functype_t *
wasm_functype_new(own wasm_valtype_vec_t *params,
                  own wasm_valtype_vec_t *results) {
  return new (std::nothrow) wasm_functype_t(params, results);
}

WASMEDGE_CAPI_EXPORT const wasm_valtype_vec_t *
wasm_functype_params(const wasm_functype_t *functype) {
  WASM_STRUCT_GET_ATTR(functype, params, nullptr, &);
}

WASMEDGE_CAPI_EXPORT const wasm_valtype_vec_t *
wasm_functype_results(const wasm_functype_t *functype) {
  WASM_STRUCT_GET_ATTR(functype, results, nullptr, &);
}

/// >>>>>>>> wasm_functype_t functions >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

/// <<<<<<<< wasm_globaltype_t functions <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

WASM_DECLARE_TYPE_IMPL(globaltype)

WASMEDGE_CAPI_EXPORT own wasm_globaltype_t *
wasm_globaltype_new(own wasm_valtype_t *valtype, wasm_mutability_t mutability) {
  return new (std::nothrow) wasm_globaltype_t(valtype, mutability);
}

WASMEDGE_CAPI_EXPORT const wasm_valtype_t *
wasm_globaltype_content(const wasm_globaltype_t *globaltype) {
  WASM_STRUCT_GET_ATTR(globaltype, content, nullptr, &);
}

WASMEDGE_CAPI_EXPORT wasm_mutability_t
wasm_globaltype_mutability(const wasm_globaltype_t *globaltype) {
  WASM_STRUCT_GET_ATTR(globaltype, mutability, WASM_CONST, );
}

/// >>>>>>>> wasm_globaltype_t functions >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

/// <<<<<<<< wasm_tabletype_t functions <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

WASM_DECLARE_TYPE_IMPL(tabletype)

WASMEDGE_CAPI_EXPORT own wasm_tabletype_t *
wasm_tabletype_new(own wasm_valtype_t *valtype, const wasm_limits_t *limits) {
  return new (std::nothrow) wasm_tabletype_t(valtype, limits);
}

WASMEDGE_CAPI_EXPORT const wasm_valtype_t *
wasm_tabletype_element(const wasm_tabletype_t *tabletype) {
  WASM_STRUCT_GET_ATTR(tabletype, valtype, nullptr, &);
}

WASMEDGE_CAPI_EXPORT const wasm_limits_t *
wasm_tabletype_limits(const wasm_tabletype_t *tabletype) {
  WASM_STRUCT_GET_ATTR(tabletype, limits, nullptr, &);
}

/// >>>>>>>> wasm_tabletype_t functions >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

/// <<<<<<<< wasm_memorytype_t functions <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

WASM_DECLARE_TYPE_IMPL(memorytype)

WASMEDGE_CAPI_EXPORT own wasm_memorytype_t *
wasm_memorytype_new(const wasm_limits_t *limit) {
  return new (std::nothrow) wasm_memorytype_t(limit);
}

WASMEDGE_CAPI_EXPORT const wasm_limits_t *
wasm_memorytype_limits(const wasm_memorytype_t *memorytype) {
  WASM_STRUCT_GET_ATTR(memorytype, limits, nullptr, &);
}

/// >>>>>>>> wasm_memorytype_t functions >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

/// <<<<<<<< wasm_externtype_t functions <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

WASM_DECLARE_TYPE_IMPL(externtype)

WASMEDGE_CAPI_EXPORT wasm_externkind_t
wasm_externtype_kind(const wasm_externtype_t *externtype) {
  WASM_STRUCT_GET_ATTR(externtype, kind, WASM_EXTERN_FUNC, );
}

WASMEDGE_CAPI_EXPORT wasm_externtype_t *
wasm_functype_as_externtype(wasm_functype_t *functype) {
  return functype;
}

WASMEDGE_CAPI_EXPORT wasm_externtype_t *
wasm_globaltype_as_externtype(wasm_globaltype_t *globaltype) {
  return globaltype;
}

WASMEDGE_CAPI_EXPORT wasm_externtype_t *
wasm_tabletype_as_externtype(wasm_tabletype_t *tabletype) {
  return tabletype;
}

WASMEDGE_CAPI_EXPORT wasm_externtype_t *
wasm_memorytype_as_externtype(wasm_memorytype_t *memorytype) {
  return memorytype;
}

WASMEDGE_CAPI_EXPORT wasm_functype_t *
wasm_externtype_as_functype(wasm_externtype_t *externtype) {
  return static_cast<wasm_functype_t *>(externtype);
}

WASMEDGE_CAPI_EXPORT wasm_globaltype_t *
wasm_externtype_as_globaltype(wasm_externtype_t *externtype) {
  return static_cast<wasm_globaltype_t *>(externtype);
}

WASMEDGE_CAPI_EXPORT wasm_tabletype_t *
wasm_externtype_as_tabletype(wasm_externtype_t *externtype) {
  return static_cast<wasm_tabletype_t *>(externtype);
}

WASMEDGE_CAPI_EXPORT wasm_memorytype_t *
wasm_externtype_as_memorytype(wasm_externtype_t *externtype) {
  return static_cast<wasm_memorytype_t *>(externtype);
}

WASMEDGE_CAPI_EXPORT const wasm_externtype_t *
wasm_functype_as_externtype_const(const wasm_functype_t *functype) {
  return functype;
}

WASMEDGE_CAPI_EXPORT const wasm_externtype_t *
wasm_globaltype_as_externtype_const(const wasm_globaltype_t *globaltype) {
  return globaltype;
}

WASMEDGE_CAPI_EXPORT const wasm_externtype_t *
wasm_tabletype_as_externtype_const(const wasm_tabletype_t *tabletype) {
  return tabletype;
}

WASMEDGE_CAPI_EXPORT const wasm_externtype_t *
wasm_memorytype_as_externtype_const(const wasm_memorytype_t *memorytype) {
  return memorytype;
}

WASMEDGE_CAPI_EXPORT const wasm_functype_t *
wasm_externtype_as_functype_const(const wasm_externtype_t *externtype) {
  return static_cast<const wasm_functype_t *>(externtype);
}

WASMEDGE_CAPI_EXPORT const wasm_globaltype_t *
wasm_externtype_as_globaltype_const(const wasm_externtype_t *externtype) {
  return static_cast<const wasm_globaltype_t *>(externtype);
}

WASMEDGE_CAPI_EXPORT const wasm_tabletype_t *
wasm_externtype_as_tabletype_const(const wasm_externtype_t *externtype) {
  return static_cast<const wasm_tabletype_t *>(externtype);
}

WASMEDGE_CAPI_EXPORT const wasm_memorytype_t *
wasm_externtype_as_memorytype_const(const wasm_externtype_t *externtype) {
  return static_cast<const wasm_memorytype_t *>(externtype);
}

/// >>>>>>>> wasm_externtype_t functions >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

/// <<<<<<<< wasm_importtype_t functions <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

WASM_DECLARE_TYPE_IMPL(importtype)

WASMEDGE_CAPI_EXPORT own wasm_importtype_t *
wasm_importtype_new(own wasm_name_t *module, own wasm_name_t *name,
                    own wasm_externtype_t *externtype) {
  return new (std::nothrow) wasm_importtype_t(module, name, externtype);
}

WASMEDGE_CAPI_EXPORT const wasm_name_t *
wasm_importtype_module(const wasm_importtype_t *importtype) {
  WASM_STRUCT_GET_ATTR(importtype, modname, nullptr, &);
}

WASMEDGE_CAPI_EXPORT const wasm_name_t *
wasm_importtype_name(const wasm_importtype_t *importtype) {
  WASM_STRUCT_GET_ATTR(importtype, name, nullptr, &);
}

WASMEDGE_CAPI_EXPORT const wasm_externtype_t *
wasm_importtype_type(const wasm_importtype_t *importtype) {
  WASM_STRUCT_GET_ATTR(importtype, type, nullptr, );
}

/// >>>>>>>> wasm_importtype_t functions >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

/// <<<<<<<< wasm_exporttype_t functions <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

WASM_DECLARE_TYPE_IMPL(exporttype)

WASMEDGE_CAPI_EXPORT own wasm_exporttype_t *
wasm_exporttype_new(own wasm_name_t *name, own wasm_externtype_t *externtype) {
  return new (std::nothrow) wasm_exporttype_t(name, externtype);
}

WASMEDGE_CAPI_EXPORT const wasm_name_t *
wasm_exporttype_name(const wasm_exporttype_t *exporttype) {
  WASM_STRUCT_GET_ATTR(exporttype, name, nullptr, &);
}

WASMEDGE_CAPI_EXPORT const wasm_externtype_t *
wasm_exporttype_type(const wasm_exporttype_t *exporttype) {
  WASM_STRUCT_GET_ATTR(exporttype, type, nullptr, );
}

/// >>>>>>>> wasm_exporttype_t functions >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

//////////// runtime objects ///////////////////////////////////////////////////

/// <<<<<<<< wasm_val_t functions <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

WASMEDGE_CAPI_EXPORT void wasm_val_delete(own wasm_val_t *val) {
  if (val) {
    switch (val->kind) {
    case WASM_I32:
    case WASM_I64:
    case WASM_F32:
    case WASM_F64:
    default:
      (val->of).i64 = 0;
      break;
    case WASM_ANYREF:
    case WASM_FUNCREF:
      wasm_ref_delete((val->of).ref);
      (val->of).ref = nullptr;
      break;
    }
  }
}

WASMEDGE_CAPI_EXPORT void wasm_val_copy(own wasm_val_t *out,
                                        const wasm_val_t *val) {
  if (out && val) {
    out->kind = val->kind;
    switch (out->kind) {
    case WASM_I32:
    case WASM_I64:
    case WASM_F32:
    case WASM_F64:
    default:
      out->of = val->of;
      break;
    case WASM_ANYREF:
    case WASM_FUNCREF:
      (out->of).ref = wasm_ref_copy((val->of).ref);
      break;
    }
  }
}

WASM_DECLARE_VEC_SCALAR_IMPL(val)

/// >>>>>>>> wasm_val_t functions >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

/// <<<<<<<< wasm_ref_t functions <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

WASM_DECLARE_REF_BASE_IMPL(ref)

/// >>>>>>>> wasm_ref_t functions >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

#ifdef own
#undef own
#endif

#ifdef __cplusplus
} /// extern "C"
#endif
