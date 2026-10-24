#pragma once

#if defined(_WIN32)
#if defined(VC_NATIVE_BUILD)
#define VC_API __declspec(dllexport)
#else
#define VC_API __declspec(dllimport)
#endif
#else
#define VC_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* The C interface the server loads visualcore through. All text is UTF-8 JSON. Strings returned by
 * these functions belong to the caller and are released with `vc_free`. A document handle is not
 * thread-safe, reads included: callers serialize every call on the same handle. */

typedef struct vc_document vc_document;

/* Loads a snapshot in the shape `snapshotToJson` writes. A null or empty snapshot opens an empty
 * document. Returns null if the snapshot is malformed. Nodes the document refuses are skipped and
 * counted in `rejected` when it is not null. `vc_last_error` then explains the malformed snapshot or
 * the first refused node. */
VC_API vc_document* vc_document_open(const char* snapshot, int* rejected);
VC_API void vc_document_close(vc_document* document);

/* Applies `ops`, an array of `{ kind, data }` like the server's op log, in order. Returns an array with
 * one entry per op: `true` when it applied, or `{ "path", "message" }` explaining why it was invalid and
 * changed nothing. Returns null, applying nothing, if the list itself is malformed; `vc_last_error`
 * then says where. */
VC_API char* vc_document_apply(vc_document* document, const char* ops);

/* Why the last `vc_document_open` or `vc_document_apply` on this thread refused its input, as
 * `{ "path", "message" }`, or null when it refused nothing. */
VC_API char* vc_last_error(void);

VC_API char* vc_document_snapshot(const vc_document* document);
/* Rows topmost first, each followed by its children, nodes copied into instances included. */
VC_API char* vc_document_layers(const vc_document* document);
/* One node with its laid-out box in `mode` (null or empty for the default mode). Null for an unknown
 * node or mode. */
VC_API char* vc_document_node(const vc_document* document, const char* id, const char* mode);
/* Tokens with their values resolved in `mode` (null or empty for the default mode). Null for an
 * unknown mode. */
VC_API char* vc_document_tokens(const vc_document* document, const char* mode);
VC_API char* vc_document_modes(const vc_document* document);

VC_API void vc_free(char* text);

#ifdef __cplusplus
}
#endif
