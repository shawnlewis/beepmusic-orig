#include <sys/queue.h>

#include "beepupdate.h"
#include "yaml_utils.h"

#define NEW_ITEM_PM ((PrivUpdateManifest *)0x6e6577)

#define UMITEM_INVALID_STR      "invalid"
#define UMITEM_FILE_STR         "file"
#define UMITEM_REMOVE_STR       "rm"
#define UMITEM_SH_SCRIPT_STR    "sh"
#define UMITEM_LUA_SCRIPT_STR   "lua"
#define UMITEM_PRI_FIRMWARE_STR "pfirm"
#define UMITEM_REC_FIRMWARE_STR "rfirm"


typedef struct {
    UpdateManifest m;
    LIST_HEAD(listhead, PrivUMItem_s) items_head;
    bool isfile;
    union {
        struct {
            FILE *stream;
            DigestMethod method;
        } file;
        struct {
            const void *ptr;
            size_t size;
        } data;
    } data;
} PrivUpdateManifest;

typedef struct PrivUMItem_s {
    UMItem i;
    PrivUpdateManifest *pm;
    LIST_ENTRY(PrivUMItem_s) items;
} PrivUMItem;


static const char *item_type_str(UMItemType type) {
    switch (type) {
        case UMITEM_INVALID:
            return UMITEM_INVALID_STR;
        case UMITEM_FILE:
            return UMITEM_FILE_STR;
        case UMITEM_REMOVE:
            return UMITEM_REMOVE_STR;
        case UMITEM_SH_SCRIPT:
            return UMITEM_SH_SCRIPT_STR;
        case UMITEM_LUA_SCRIPT:
            return UMITEM_LUA_SCRIPT_STR;
        case UMITEM_PRI_FIRMWARE:
            return UMITEM_PRI_FIRMWARE_STR;
        case UMITEM_REC_FIRMWARE:
            return UMITEM_REC_FIRMWARE_STR;
        default:
            return "unknown";
    }
}

static UMItemType str_to_item_type(const char *str) {
    if (!strcmp(str, UMITEM_FILE_STR)) {
        return UMITEM_FILE;
    } else if (!strcmp(str, UMITEM_REMOVE_STR)) {
        return UMITEM_REMOVE;
    } else if (!strcmp(str, UMITEM_SH_SCRIPT_STR)) {
        return UMITEM_SH_SCRIPT;
    } else if (!strcmp(str, UMITEM_LUA_SCRIPT_STR)) {
        return UMITEM_LUA_SCRIPT;
    } else if (!strcmp(str, UMITEM_PRI_FIRMWARE_STR)) {
        return UMITEM_PRI_FIRMWARE;
    } else if (!strcmp(str, UMITEM_REC_FIRMWARE_STR)) {
        return UMITEM_REC_FIRMWARE;
    } else {
        return UMITEM_INVALID;
    }
}

void dump_manifest(UpdateManifest *manifest) {
    PrivUpdateManifest *pm = (PrivUpdateManifest *)manifest;
    PrivUMItem *pitem;
    int index;

    if (!manifest) {
        LOG_ERROR("manifest is NULL");
        return;
    }

    LOG_INFO("PrivUpdateManifest: %p", pm);
    LOG_INFO("  pm->m.last_modified: %" PRIu64, pm->m.last_modified);
    LOG_INFO("  pm->m.count: %d", pm->m.count);
    LOG_INFO("  pm->m.modified: %d", pm->m.modified);
    LOG_INFO("  pm->items_head.lh_first: %p", pm->items_head.lh_first);
    LOG_INFO("  pm->isfile: %d", pm->isfile);
    if (pm->isfile) {
        LOG_INFO("  pm->data.file.stream: %p", pm->data.file.stream);
        LOG_INFO("  pm->data.file.method: %d (%c)", pm->data.file.method,
                (pm->data.file.method >= '!' && pm->data.file.method <= '~') ?
                pm->data.file.method : ' ');
    } else {
        LOG_INFO("  pm->data.data.ptr: %p", pm->data.data.ptr);
        LOG_INFO("  pm->data.data.size: %zu", pm->data.data.size);
    }
    for (pitem = pm->items_head.lh_first, index = 0;
            pitem != NULL;
            pitem = pitem->items.le_next, index++) {
        LOG_INFO("  PrivUMItem (%d): %p", index, pitem);
        // TODOJOE: Add dump_dig_list later.
        LOG_INFO("    pitem->i.dig_list: %p", pitem->i.dig_list);
        LOG_INFO("    pitem->i.path: %s", pitem->i.path);
        LOG_INFO("    pitem->i.flags: 0x%08x", pitem->i.flags);
        LOG_INFO("    pitem->i.type: %d (%s)", pitem->i.type,
                item_type_str(pitem->i.type));
        //LOG_INFO("    pitem->items.le_next: %p", pitem->items.le_next);
        //LOG_INFO("    pitem->items.le_prev: %p", pitem->items.le_prev);
    }
}

static void cleanup_item(PrivUMItem *pitem) {
    if (pitem) {
        if (pitem->i.dig_list) {
            dig_list_free(pitem->i.dig_list);
        }
        if (pitem->i.path) {
            bfree(pitem->i.path);
        }
        bfree(pitem);
    }
}

static void cleanup_manifest(PrivUpdateManifest *pm) {
    PrivUMItem *pitem;
    if (pm) {
        while (pm->items_head.lh_first) {
            pitem = pm->items_head.lh_first;
            LIST_REMOVE(pm->items_head.lh_first, items);
            cleanup_item(pitem);
        }
        bfree(pm);
    }
}

static bool PrivUMItemFlagsValid(PrivUMItem *pitem) {
    switch (pitem->i.type) {
    case UMITEM_FILE:
        if (!(pitem->i.flags & UMITEM_FLAGS_FILE_INVALID_MASK)) {
            return true;
        }
        break;

    case UMITEM_REMOVE:
        if (!(pitem->i.flags & UMITEM_FLAGS_REMOVE_INVALID_MASK)) {
            return true;
        }
        break;

    case UMITEM_SH_SCRIPT:
        if (!(pitem->i.flags & UMITEM_FLAGS_SH_SCRIPT_INVALID_MASK)) {
            return true;
        }
        break;

    case UMITEM_LUA_SCRIPT:
        if (!(pitem->i.flags & UMITEM_FLAGS_LUA_SCRIPT_INVALID_MASK)) {
            return true;
        }
        break;

    case UMITEM_PRI_FIRMWARE:
        if (!(pitem->i.flags & UMITEM_FLAGS_PRI_FIRMWARE_INVALID_MASK)) {
            return true;
        }
        break;

    case UMITEM_REC_FIRMWARE:
        if (!(pitem->i.flags & UMITEM_FLAGS_REC_FIRMWARE_INVALID_MASK)) {
            return true;
        }
        break;

    default:
        break;
    }

    return false;
}

static bool PrivUMItemValid(PrivUMItem *pitem) {
    if (pitem) {
        if (!PrivUMItemFlagsValid(pitem)) {
            return false;
        }

        switch (pitem->i.type) {
        case UMITEM_FILE:
        case UMITEM_SH_SCRIPT:
        case UMITEM_LUA_SCRIPT:
        case UMITEM_PRI_FIRMWARE:
        case UMITEM_REC_FIRMWARE:
            if (!pitem->i.path
                    || !pitem->i.dig_list) {
                return false;
            }
            return true;
            break;

        case UMITEM_REMOVE:
            if (!pitem->i.path) {
                return false;
            }
            return true;
            break;

        default:
            return false;
            break;
        }
    }

    return false;
}

static PrivUpdateManifest *parse_manifest(yaml_parser_t *parser,
        UpdateCode *code) {
    enum {
        TL_MAP_START,
        TL_MAP_KEYS,
        MANIFEST_MAP_START,
        MANIFEST_MAP_KEYS,
        MANIFEST_MAP_VALUE_MILLIS,
        FILES_SEQ_START,
        FILES_SEQ_MEMBERS,
        FILE_MAP_START,
        FILE_MAP_KEYS,
        FILE_MAP_VALUE_PATH,
        FILE_MAP_VALUE_DIGEST_SEQ_START,
        FILE_MAP_VALUE_DIGEST_MEMBERS,
        FILE_MAP_VALUE_FLAGS,
        FILE_MAP_VALUE_TYPE,
        __IGNORE_BLOCK,
        __UNKNOWN_STATE
    };

    PrivUpdateManifest *pm;
    PrivUMItem *pitem = NULL;
    UpdateCode rcode = UPDATE_OK;
    char *ccheck = NULL;

    yaml_event_t event;
    ParseControl *ctrl;
    PARSE_CONTROL_INIT(ctrl, TL_MAP_START);

    pm = (PrivUpdateManifest *)bmalloc(sizeof(PrivUpdateManifest));
    if (!ctrl || !pm) {
        PARSE_CONTROL_CLEANUP(ctrl);
        if (pm)
            bfree(pm);
        *code = UPDATE_OOM;
        return NULL;
    }
    LIST_INIT(&pm->items_head);

    while (ctrl->p_err == POK_READY) {
        if (!yaml_parser_parse(parser, &event)) {
            PARSE_SET_ERROR(ctrl, PERR_YAML);
            break;
        }

        PARSE_DUMP_STATE(ctrl, event);

        if (event.type == YAML_STREAM_END_EVENT) {
            yaml_event_delete(&event);
            ctrl->count++;
            PARSE_SET_ERROR(ctrl, PERR_STREAM_END);
            break;
        }

        switch (PARSE_CURRENT_STATE(ctrl)) {
        case TL_MAP_START: {
            if (event.type == YAML_MAPPING_START_EVENT) {
                PARSE_CHANGE_STATE(ctrl, TL_MAP_KEYS);
            } else if (event.type == YAML_STREAM_START_EVENT ||
                    event.type == YAML_DOCUMENT_START_EVENT) {
                // Ignore the stream/document start events.
                continue;
            } else {
                PARSE_UNEXPECTED_EVENT(ctrl, event, YAML_MAPPING_START_EVENT);
            }
            break;
        }

        case TL_MAP_KEYS: {
            if (event.type == YAML_SCALAR_EVENT) {
                if (PARSE_SCALAR_EQ("manifest", event)) {
                    PARSE_PUSH_STATE(ctrl, MANIFEST_MAP_START);
                } else if (PARSE_SCALAR_EQ("files", event)) {
                    PARSE_PUSH_STATE(ctrl, FILES_SEQ_START);
                } else {
                    PARSE_IGNORE_BLOCK(ctrl);
                }
            } else if (event.type == YAML_MAPPING_END_EVENT) {
                PARSE_DONE(ctrl);
            } else {
                PARSE_UNEXPECTED_EVENT(ctrl, event, YAML_SCALAR_EVENT);
            }
            break;
        }

        case MANIFEST_MAP_START: {
            if (event.type == YAML_MAPPING_START_EVENT) {
                PARSE_CHANGE_STATE(ctrl, MANIFEST_MAP_KEYS);
            } else {
                PARSE_UNEXPECTED_EVENT(ctrl, event,
                        YAML_MAPPING_START_EVENT);
            }
            break;
        }

        case MANIFEST_MAP_KEYS: {
            if (event.type == YAML_SCALAR_EVENT) {
                if (PARSE_SCALAR_EQ("millis", event)) {
                    PARSE_PUSH_STATE(ctrl, MANIFEST_MAP_VALUE_MILLIS);
                } else {
                    PARSE_IGNORE_BLOCK(ctrl);
                }
            } else if (event.type == YAML_MAPPING_END_EVENT) {
                PARSE_POP_STATE(ctrl);
            } else {
                PARSE_UNEXPECTED_EVENT(ctrl, event, YAML_SCALAR_EVENT);
            }
            break;
        }

        case MANIFEST_MAP_VALUE_MILLIS: {
            if (event.type == YAML_SCALAR_EVENT) {
                pm->m.last_modified = strtoll(
                        (const char *)event.data.scalar.value, &ccheck, 10);
                if (*ccheck != '\0') {
                    PARSE_SET_ERROR(ctrl, PERR_PARSE);
                }
                PARSE_POP_STATE(ctrl);
            } else {
                PARSE_UNEXPECTED_EVENT(ctrl, event, YAML_SCALAR_EVENT);
            }
            break;
        }

        case FILES_SEQ_START: {
            if (event.type == YAML_SEQUENCE_START_EVENT) {
                PARSE_CHANGE_STATE(ctrl, FILES_SEQ_MEMBERS);
            } else if (event.type == YAML_SCALAR_EVENT &&
                    event.data.scalar.value[0] == '\0') {
                PARSE_POP_STATE(ctrl);
            } else {
                PARSE_UNEXPECTED_EVENT(ctrl, event,
                        YAML_SEQUENCE_START_EVENT);
            }
            break;
        }

        case FILES_SEQ_MEMBERS: {
            if (event.type == YAML_MAPPING_START_EVENT) {
                PARSE_PUSH_STATE(ctrl, FILE_MAP_KEYS);
            } else if (event.type == YAML_SEQUENCE_END_EVENT) {
                PARSE_POP_STATE(ctrl);
            } else {
                PARSE_UNEXPECTED_EVENT(ctrl, event, YAML_MAPPING_START_EVENT);
            }
            break;
        }

        case FILE_MAP_KEYS: {
            if (event.type == YAML_SCALAR_EVENT) {
                if (!pitem) {
                    pitem = (PrivUMItem *)manifest_item_new();
                    if (!pitem) {
                        PARSE_SET_ERROR(ctrl, PERR_SYSTEM);
                        rcode = UPDATE_OOM;
                        break;
                    }
                }

                if (PARSE_SCALAR_EQ("path", event)) {
                    PARSE_PUSH_STATE(ctrl, FILE_MAP_VALUE_PATH);
                } else if (PARSE_SCALAR_EQ("digest", event)) {
                    PARSE_PUSH_STATE(ctrl, FILE_MAP_VALUE_DIGEST_SEQ_START);
                } else if (PARSE_SCALAR_EQ("flags", event)) {
                    PARSE_PUSH_STATE(ctrl, FILE_MAP_VALUE_FLAGS);
                } else if (PARSE_SCALAR_EQ("type", event)) {
                    PARSE_PUSH_STATE(ctrl, FILE_MAP_VALUE_TYPE);
                } else {
                    PARSE_IGNORE_BLOCK(ctrl);
                }
            } else if (event.type == YAML_MAPPING_END_EVENT) {
                if (PrivUMItemValid(pitem)) {
                    rcode = manifest_insert((UpdateManifest *)pm,
                            (UMItem *)pitem, INSERT_LAST);
                    if (rcode == UPDATE_OK) {
                        pitem = NULL;
                    } else {
                        PARSE_SET_ERROR(ctrl, PERR_SYSTEM);
                    }
                } else {
                    PARSE_SET_ERROR(ctrl, PERR_PARSE);
                }
                PARSE_POP_STATE(ctrl);
            } else {
                PARSE_UNEXPECTED_EVENT(ctrl, event, YAML_SCALAR_EVENT);
            }
            break;
        }

        case FILE_MAP_VALUE_PATH: {
            if (event.type == YAML_SCALAR_EVENT) {
                pitem->i.path = bstrdup((const char *)event.data.scalar.value);
                PARSE_POP_STATE(ctrl);
            } else {
                PARSE_UNEXPECTED_EVENT(ctrl, event, YAML_SCALAR_EVENT);
            }
            break;
        }

        case FILE_MAP_VALUE_DIGEST_SEQ_START: {
            if (event.type == YAML_SEQUENCE_START_EVENT) {
                PARSE_CHANGE_STATE(ctrl, FILE_MAP_VALUE_DIGEST_MEMBERS);
            } else if (event.type == YAML_SCALAR_EVENT &&
                    event.data.scalar.value[0] == '\0') {
                PARSE_POP_STATE(ctrl);
            } else {
                PARSE_UNEXPECTED_EVENT(ctrl, event,
                        YAML_SEQUENCE_START_EVENT);
            }
            break;
        }

        case FILE_MAP_VALUE_DIGEST_MEMBERS: {
            if (event.type == YAML_SCALAR_EVENT) {
                // We ignore the return code here since there may be newer
                // digest types added.  We will check that there at least
                // one dig entry in dig_list in PrivUMItemValid.
                read_dig_entry_manifest(event.data.scalar.value,
                        event.data.scalar.length, &pitem->i.dig_list);
            } else if (event.type == YAML_SEQUENCE_END_EVENT) {
                PARSE_POP_STATE(ctrl);
            } else {
                PARSE_UNEXPECTED_EVENT(ctrl, event, YAML_SCALAR_EVENT);
            }
            break;
        }

        case FILE_MAP_VALUE_FLAGS: {
            if (event.type == YAML_SCALAR_EVENT) {
                if (!strict_hex_to_uint32(&pitem->i.flags,
                        (const char *)event.data.scalar.value)) {
                    pitem->i.flags = UMITEM_FLAGS_INVALID;
                }
                PARSE_POP_STATE(ctrl);
            } else {
                PARSE_UNEXPECTED_EVENT(ctrl, event, YAML_SCALAR_EVENT);
            }
            break;
        }

        case FILE_MAP_VALUE_TYPE: {
            if (event.type == YAML_SCALAR_EVENT) {
                pitem->i.type = str_to_item_type(
                        (const char *)event.data.scalar.value);
                PARSE_POP_STATE(ctrl);
            } else {
                PARSE_UNEXPECTED_EVENT(ctrl, event, YAML_SCALAR_EVENT);
            }
            break;
        }

        case __IGNORE_BLOCK: {
            PARSE_IGNORE_CASE(event, ctrl, ignore_depth);
            break;
        }

        default:
            PARSE_SET_ERROR(ctrl, PERR_PARSE);
            PARSE_CHANGE_STATE(ctrl, __UNKNOWN_STATE);
            break;
        }

        yaml_event_delete(&event);
        ctrl->count++;
    }

    if (ctrl->p_err != POK_READY && ctrl->p_err != POK_DONE) {
        log_parse_error(ctrl);
        if (rcode == UPDATE_OK)
            rcode = UPDATE_YAML_ERR;
    }

    if (pitem)
        cleanup_item(pitem);

    PARSE_CONTROL_CLEANUP(ctrl);

    *code = rcode;

    return pm;
}

UpdateManifest *open_file_manifest(FILE *stream, UpdateCode *code) {
    PrivUpdateManifest *pm = NULL;
    yaml_parser_t parser;
    long start_pos;
    UpdateCode rcode = UPDATE_OK;
    DigestMethod method;

    if (!stream) {
        rcode = UPDATE_BAD_ARG;
        goto done;
    }

    start_pos = ftell(stream);

    rcode = verify_embedded_file(stream, FILE_TYPE_YML, false, &method);
    if (rcode != UPDATE_OK) {
        LOG_ERROR("digest failed");
        goto done;
    }

    if (!yaml_parser_initialize(&parser)) {
        rcode = UPDATE_YAML_ERR;
        goto done;
    }
    yaml_parser_set_input_file(&parser, stream);

    pm = parse_manifest(&parser, &rcode);

    if (pm) {
        pm->isfile = true;
        pm->data.file.stream = stream;
        pm->data.file.method = method;
    }

    yaml_parser_delete(&parser);
    fseek(stream, start_pos, SEEK_SET);

done:
    if (code)
        *code = rcode;

    return (UpdateManifest *)pm;
}

UpdateManifest *open_mem_manifest(const void *data, size_t size,
        UpdateCode *code) {
    PrivUpdateManifest *pm = NULL;
    yaml_parser_t parser;
    UpdateCode rcode = UPDATE_OK;

    if (!data) {
        rcode = UPDATE_BAD_ARG;
        goto done;
    }

    rcode = verify_embedded_mem(data, size, FILE_TYPE_YML, false, NULL);
    if (rcode != UPDATE_OK) {
        LOG_ERROR("digest failed");
        goto done;
    }

    if (!yaml_parser_initialize(&parser)) {
        rcode = UPDATE_YAML_ERR;
        goto done;
    }
    yaml_parser_set_input_string(&parser, data, size);

    pm = parse_manifest(&parser, &rcode);

    if (pm) {
        pm->isfile = false;
        pm->data.data.ptr = data;
        pm->data.data.size = size;
    }

    yaml_parser_delete(&parser);

done:
    if (code)
        *code = rcode;
    return (UpdateManifest *)pm;
}

static UpdateCode emit_manifest(yaml_emitter_t *emitter,
        PrivUpdateManifest *pm) {
    PrivUMItem *pitem;
    yaml_event_t event;
    int line = -1;
    char millis_str[22];
    //char *digstr;

    if (snprintf(millis_str, sizeof(millis_str), "%" PRIu64,
            pm->m.last_modified) < 0) {
        line = __LINE__ - 1;
        goto done;
    }

    // start
    EMIT_EVENT(emitter, &event, stream_start, true, YAML_UTF8_ENCODING);
    EMIT_EVENT(emitter, &event, document_start, true,
            NULL, NULL, NULL, 0);
    EMIT_EVENT(emitter, &event, mapping_start, true,
            NULL, NULL, 1, YAML_BLOCK_MAPPING_STYLE);

    // manifest map start
    EMIT_EVENT(emitter, &event, scalar, true,
            NULL, NULL, (yaml_char_t *)"manifest", 8, 1, 0,
            YAML_PLAIN_SCALAR_STYLE);
    EMIT_EVENT(emitter, &event, mapping_start, true,
            NULL, NULL, 1, YAML_BLOCK_MAPPING_STYLE);

    // millis
    EMIT_EVENT(emitter, &event, scalar, true,
            NULL, NULL, (yaml_char_t *)"millis", 6, 1, 0,
            YAML_PLAIN_SCALAR_STYLE);
    EMIT_EVENT(emitter, &event, scalar, true,
            NULL, NULL, (yaml_char_t *)millis_str, strlen(millis_str), 1, 0,
            YAML_PLAIN_SCALAR_STYLE);

    // manifest map end
    EMIT_EVENT(emitter, &event, mapping_end, true);

    // files seq start
    EMIT_EVENT(emitter, &event, scalar, true,
            NULL, NULL, (yaml_char_t *)"files", 5, 1, 0,
            YAML_PLAIN_SCALAR_STYLE);
    EMIT_EVENT(emitter, &event, sequence_start, true,
            NULL, NULL, 1, YAML_BLOCK_MAPPING_STYLE);

    for (pitem = pm->items_head.lh_first;
            pitem != NULL;
            pitem = pitem->items.le_next) {
        // file map start
        EMIT_EVENT(emitter, &event, mapping_start, true,
                NULL, NULL, 1, YAML_BLOCK_MAPPING_STYLE);

        // path
        EMIT_EVENT(emitter, &event, scalar, true,
                NULL, NULL, (yaml_char_t *)"path", 4, 1, 0,
                YAML_PLAIN_SCALAR_STYLE);
        EMIT_EVENT(emitter, &event, scalar, true,
                NULL, NULL, (yaml_char_t *)pitem->i.path,
                strlen(pitem->i.path), 1, 0,
                YAML_PLAIN_SCALAR_STYLE);

        // flags
        // Only emit flags if actually defined.
        if (pitem->i.flags) {
            char flags_str[11];

            if (!PrivUMItemFlagsValid(pitem)) {
                line = __LINE__ - 1;
                goto done;
            }

            if (snprintf(flags_str, sizeof(flags_str), "0x%08x",
                    pitem->i.flags) < 0) {
                line = __LINE__ - 1;
                goto done;
            }

            EMIT_EVENT(emitter, &event, scalar, true,
                    NULL, NULL, (yaml_char_t *)"flags", 5, 1, 0,
                    YAML_PLAIN_SCALAR_STYLE);
            EMIT_EVENT(emitter, &event, scalar, true,
                    NULL, NULL, (yaml_char_t *)flags_str,
                    strlen(flags_str), 1, 0,
                    YAML_PLAIN_SCALAR_STYLE);
        }

        // TODOJOE: Fix me later.
        // dig
        //if (pitem->i.digest) {
        //    EMIT_EVENT(emitter, &event, scalar, true,
        //            NULL, NULL, (yaml_char_t *)"dig", 3, 1, 0,
        //            YAML_PLAIN_SCALAR_STYLE);

        //    digstr = hex_method_to_digstr(pitem->i.digest, pitem->i.method);
        //    if (digstr) {
        //        EMIT_EVENT(emitter, &event, scalar, true,
        //                NULL, NULL, (yaml_char_t *)digstr, strlen(digstr), 1, 0,
        //                YAML_PLAIN_SCALAR_STYLE);
        //        bfree(digstr);
        //    } else {
        //        EMIT_EVENT(emitter, &event, scalar, true,
        //                NULL, NULL, (yaml_char_t *)"dig", 3, 1, 0,
        //                YAML_PLAIN_SCALAR_STYLE);
        //    }
        //}

        // type
        EMIT_EVENT(emitter, &event, scalar, true,
                NULL, NULL, (yaml_char_t *)"type", 4, 1, 0,
                YAML_PLAIN_SCALAR_STYLE);
        EMIT_EVENT(emitter, &event, scalar, true,
                NULL, NULL, (yaml_char_t *)item_type_str(pitem->i.type),
                strlen(item_type_str(pitem->i.type)), 1, 0,
                YAML_PLAIN_SCALAR_STYLE);

        // file map end
        EMIT_EVENT(emitter, &event, mapping_end, true);
    }

    // files seq end
    EMIT_EVENT(emitter, &event, sequence_end, true);

    // end
    EMIT_EVENT(emitter, &event, mapping_end, true);
    EMIT_EVENT(emitter, &event, document_end, true, 1);
    EMIT_EVENT(emitter, &event, stream_end, true);

done:
    return (line == -1) ? UPDATE_OK : UPDATE_ERR;
}

FILE *save_manifest(UpdateManifest *manifest, FILE *stream,
        DigestMethod method, UpdateCode *code) {
    PrivUpdateManifest *pm = (PrivUpdateManifest *)manifest;
    PrivUMItem *pitem = NULL;
    yaml_emitter_t emitter;
    char *fp = NULL;
    char *hdr = NULL;
    char *dighex = NULL;
    char *digstr = NULL;
    size_t hdr_size;
    size_t wsize;
    int index;
    UpdateCode rcode = UPDATE_OK;

    if (!pm || (!pm->isfile && !stream)) {
        rcode = UPDATE_BAD_ARG;
        stream = NULL;
        goto done;
    }

    // Use stream/method args only if stream is not NULL, else use args used
    // to open manifest.
    method = stream ? method : pm->data.file.method;
    stream = stream ? stream : pm->data.file.stream;

    // Check method is valid and something we can generate.
    if (!is_hash(method)) {
        rcode = UPDATE_DIG_FAIL;
        goto done;
    }

    if (!stream) {
        LOG_ERROR("cannot reopen NULL stream");
        rcode = UPDATE_FILE_ERR;
        goto done;
    }

    for (pitem = pm->items_head.lh_first, index = 0;
            pitem != NULL;
            pitem = pitem->items.le_next, index++) {
        if (!PrivUMItemValid(pitem)) {
            LOG_ERROR("item %d has incorrect data", index);
            rcode = UPDATE_INVALID_STATE;
            goto done;
        }
    }
    if (index != pm->m.count) {
        LOG_ERROR("actual num items %d does not match count %d", index,
                pm->m.count);
        rcode = UPDATE_INVALID_STATE;
        goto done;
    }

    fp = fpath(stream);
    if (!fp) {
        rcode = UPDATE_FILE_ERR;
        goto done;
    }

    if (!yaml_emitter_initialize(&emitter)) {
        rcode = UPDATE_YAML_ERR;
        goto done;
    }

    // TODOJOE: Fix me later.
    // Alloc space for the digstr header.
    hdr_size = MAX_DIGEST_HEX_SIZE + 5;  //method_to_hex_size(method) + 5;
    hdr = bmalloc(hdr_size + 1);  // hdr + '\0'
    if (!hdr) {
        rcode = UPDATE_OOM;
        goto done;
    }

    // All checks need to be done by this point as the file is about to be
    // truncated.
    stream = freopen(fp, "w+", stream);
    if (!stream) {
        rcode = UPDATE_FILE_ERR;
        goto done;
    }
    // Pad out for the digstr header.
    fseek(stream, hdr_size, SEEK_SET);

    yaml_emitter_set_output_file(&emitter, stream);
    rcode = emit_manifest(&emitter, pm);
    yaml_emitter_delete(&emitter);
    if (rcode != UPDATE_OK)
        goto done;

    // Create the digest and header string.
    fseek(stream, hdr_size, SEEK_SET);

    // TODOJOE: Fix me later.
    //dighex = create_file_hex_hash(stream, FILE_END, method, NULL);
    dighex = NULL;
    if (!dighex) {
        rcode = UPDATE_DIG_FAIL;
        goto done;
    }
    //digstr = hex_method_to_digstr(dighex, method);
    if (!digstr) {
        rcode = UPDATE_DIG_FAIL;
        goto done;
    }
    if (snprintf(hdr, hdr_size + 1, "# %s\n", digstr) < 0) {
        rcode = UPDATE_OOM;
        goto done;
    }

    fseek(stream, 0, SEEK_SET);
    wsize = fwrite(hdr, 1, hdr_size, stream);
    if (wsize != hdr_size) {
        rcode = UPDATE_FILE_ERR;
        goto done;
    }

    stream = freopen(fp, "r", stream);
    if (!stream)
        rcode = UPDATE_FILE_ERR;

done:
    if (fp)
        bfree(fp);
    if (hdr)
        bfree(hdr);
    if (dighex)
        bfree(dighex);
    if (digstr)
        bfree(digstr);

    if (code)
        *code = rcode;

    return stream;
}

UpdateCode close_manifest(UpdateManifest *manifest) {
    PrivUpdateManifest *pm = (PrivUpdateManifest *)manifest;

    if (!pm)
        return UPDATE_BAD_ARG;

    cleanup_manifest(pm);

    return UPDATE_OK;
}

UMItem *manifest_item_new(void) {
    PrivUMItem *pitem = (PrivUMItem *)bmalloc(sizeof(PrivUMItem));
    if (pitem)
        pitem->pm = NEW_ITEM_PM;
    return (UMItem *)pitem;
}

UpdateCode manifest_item_free(UMItem *item) {
    PrivUMItem *pitem = (PrivUMItem *)item;
    if (!pitem || pitem->pm != NEW_ITEM_PM)
        return UPDATE_BAD_ARG;
    cleanup_item(pitem);
    return UPDATE_OK;
}

UMItem *manifest_get(UpdateManifest *manifest, int index, UpdateCode *code) {
    PrivUpdateManifest *pm = (PrivUpdateManifest *)manifest;
    PrivUMItem *pitem = NULL;
    UpdateCode rcode = UPDATE_OK;
    int tindex;

    if (!pm) {
        rcode = UPDATE_BAD_ARG;
        goto done;
    }

    if (pm->m.count == 0 || index >= pm->m.count) {
        rcode = UPDATE_OOR;
        goto done;
    }

    // INSERT_LAST == -2 but cover bad values and set to index to last.
    index = (index <= INSERT_LAST) ? pm->m.count - 1 : index;

    for (pitem = pm->items_head.lh_first, tindex = 0;
            pitem != NULL && tindex != index;
            pitem = pitem->items.le_next, tindex++);

    // pm->m.count must be wrong for this to happen.
    if (!pitem) {
        LOG_ERROR("item %d not found in list size %d", index, pm->m.count);
        rcode = UPDATE_ERR;
    }

done:
    if (code)
        *code = rcode;
    return (UMItem *)pitem;
}

UpdateCode manifest_get_index(UpdateManifest *manifest, UMItem *item,
        int *index) {
    PrivUpdateManifest *pm = (PrivUpdateManifest *)manifest;
    PrivUMItem *pitem = (PrivUMItem *)item;
    PrivUMItem *tpitem;
    int tindex;

    if (!pm || !pitem || !index || pitem->pm != pm)
        return UPDATE_BAD_ARG;

    if (pm->m.count == 0)
        return UPDATE_OOR;

    for (tpitem = pm->items_head.lh_first, tindex = 0;
            tpitem != NULL && tpitem != pitem;
            tpitem = tpitem->items.le_next, tindex++);

    if (tpitem == pitem) {
        *index = tindex;
        return UPDATE_OK;
    }

    return UPDATE_ERR;
}

UpdateCode manifest_delete(UpdateManifest *manifest, UMItem *item) {
    PrivUpdateManifest *pm = (PrivUpdateManifest *)manifest;
    PrivUMItem *pitem = (PrivUMItem *)item;

    if (!pm || !pitem || pitem->pm != pm)
        return UPDATE_BAD_ARG;

    LIST_REMOVE(pitem, items);
    pm->m.count--;
    cleanup_item(pitem);

    return UPDATE_OK;
}

UpdateCode manifest_insert(UpdateManifest *manifest, UMItem *item, int index) {
    PrivUpdateManifest *pm = (PrivUpdateManifest *)manifest;
    PrivUMItem *pitem = (PrivUMItem *)item;

    if (!pm || !pitem || pitem->pm != NEW_ITEM_PM)
        return UPDATE_BAD_ARG;

    if (index == -1 || pm->m.count == 0) {
        LIST_INSERT_HEAD(&pm->items_head, pitem, items);
    } else {
        int tindex;
        PrivUMItem *tpitem;

        // INSERT_LAST == -2 but cover bad values and set to index to last.
        index = (index <= INSERT_LAST) ? pm->m.count - 1 : index;

        for (tpitem = pm->items_head.lh_first, tindex = 0;
                tpitem->items.le_next != NULL && tindex != index;
                tpitem = tpitem->items.le_next, tindex++);

        LIST_INSERT_AFTER(tpitem, pitem, items);
    }

    pitem->pm = pm;
    pm->m.count++;

    return UPDATE_OK;
}
