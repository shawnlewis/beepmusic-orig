#include "beepupdate.h"
#include "yaml_utils.h"


const char *yaml_event_str(yaml_event_type_t e) {
    switch (e) {
    case YAML_NO_EVENT:
        return "NO";
    case YAML_STREAM_START_EVENT:
        return "STREAM_START";
    case YAML_STREAM_END_EVENT:
        return "STREAM_END";
    case YAML_DOCUMENT_START_EVENT:
        return "DOCUMENT_START";
    case YAML_DOCUMENT_END_EVENT:
        return "DOCUMENT_END";
    case YAML_ALIAS_EVENT:
        return "ALIAS";
    case YAML_SCALAR_EVENT:
        return "SCALAR";
    case YAML_SEQUENCE_START_EVENT:
        return "SEQUENCE_START";
    case YAML_SEQUENCE_END_EVENT:
        return "SEQUENCE_END";
    case YAML_MAPPING_START_EVENT:
        return "MAPPING_START";
    case YAML_MAPPING_END_EVENT:
        return "MAPPING_END";
    default:
        return "INVALID";
    }
}

void log_parse_error(ParseControl *ctrl) {
    const char *err_str;

    switch (ctrl->p_err) {
        case POK_READY: err_str = "POK_READY"; break;
        case POK_DONE: err_str = "POK_DONE"; break;
        case PERR_STREAM_END: err_str = "PERR_STREAM_END"; break;
        case PERR_YAML: err_str = "PERR_YAML"; break;
        case PERR_UNEXPECTED_EVENT: err_str = "PERR_UNEXPECTED_EVENT"; break;
        case PERR_PARSE: err_str = "PERR_PARSE"; break;
        case PERR_SYSTEM: err_str = "PERR_SYSTEM"; break;
        default: err_str = "UNKNOWN"; break;
    }

    if (ctrl->p_err == PERR_UNEXPECTED_EVENT) {
        LOG_ERROR("parse error: %s - received event %s expected %s at line %d",
                err_str,
                yaml_event_str(ctrl->err_data.unexpected.recv),
                yaml_event_str(ctrl->err_data.unexpected.exp),
                ctrl->err_line);
    } else {
        LOG_ERROR("parse error: %s at line %d", err_str, ctrl->err_line);
    }
}

int yaml_null_write_handler(void *data, unsigned char *buffer, size_t size) {
    return 1;
}
