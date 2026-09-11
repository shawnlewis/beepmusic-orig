#include <arpa/inet.h>
#include <unistd.h>
#include <libubox/blobmsg_json.h>
#include <libubox/uloop.h>
#include <libubox/list.h>
#include <libubus.h>
#include <dns_sd.h>
#include <netdb.h>
//#include <netinet/in.h>

#include "beep/beep_ubus.h"
#include "beep/config.h"
#include "beep/debug.h"
#include "beep/flags.h"
#include "beep/urelay.h"
#include "beep/beep_http.h"
#include "beep/txt_record.h"

#define DEVICE_ID "device_id"
#define CLUSTER_ID "cluster_id"
#define FRIENDLY_NAME "name"
#define VIRTUAL "virtual"

#define RESOLVE_AND_LOOKUP_TIMEOUT 90 * 1000

///// Flags

struct flag_vals {
    int control_port;
    int uhttpd_port;
    bool virtual;
};

// With default values;
struct flag_vals beepdiscovery_flags = {
    .control_port = URELAY_PORT,
    .uhttpd_port = BEEP_HTTP_PORT,
    .virtual = false,
};

static const BeepFlag flags[] = {
    BEEP_FLAG("control_port", BEEP_FLAG_INT, &beepdiscovery_flags.control_port, NULL, NULL),
    BEEP_FLAG("uhttpd_port", BEEP_FLAG_INT, &beepdiscovery_flags.uhttpd_port, NULL, NULL),
    BEEP_FLAG("virtual", BEEP_FLAG_BOOL, &beepdiscovery_flags.virtual, NULL, NULL)
};

BEEP_INIT_FLAGS(flags);

///// End Flags

#define SERVICE_NAME_MAX_LEN 128

struct fd_bridge {
    struct uloop_fd uloop_fd;
    DNSServiceRef sd_ref;
    bool browsing;
    struct uloop_timeout timeout;

    char service_name[SERVICE_NAME_MAX_LEN];
    int port;
    char device_id[BEEP_UBUS_DEV_ID_MAX_LENGTH];
};

//struct getaddrinfo_ctx {
//    struct fd_bridge fd_bridge;
//    char service_name[SERVICE_NAME_MAX_LEN];
//};

static const char *UBUS_EVENT_ADD = "beep.device.add";
static const char *UBUS_EVENT_DEL = "beep.device.delete";

static DNSServiceRef sdRefControlService;
static DNSServiceRef sdRefHeadService;

char local_id[100];
char cluster_id[100];
char friendly_name[100];

static struct ubus_context *ctx;
static struct fd_bridge browse_fd;
static struct blob_buf b;

static void uloop_mdns_cb(struct uloop_fd *fd, unsigned int events) {
    struct fd_bridge *resolver_fd = \
        container_of(fd, struct fd_bridge, uloop_fd);
    int ret = DNSServiceProcessResult(resolver_fd->sd_ref);
    if(ret) {
        LOG_ERROR(log_beep_main, "DNSServiceProcessResult failed: %d", ret);
        abort();
    }
}

static void addrinfo_cb(DNSServiceRef sdRef,
   DNSServiceFlags flags, uint32_t interfaceIndex,
   DNSServiceErrorType errorCode, const char *hostname,
   const struct sockaddr *address, uint32_t ttl, void *context)
{
    // TODO: We shouldn't ignore the ttl field.
    struct fd_bridge *resolver_fd = (struct fd_bridge*) context;
    char ip[INET_ADDRSTRLEN];

    DNSServiceRefDeallocate(resolver_fd->sd_ref);
    uloop_fd_delete(&resolver_fd->uloop_fd);

    if (errorCode) {
        LOG_ERROR(log_beep_main,
                "addrinfo_cb received error code: %d", errorCode);
        goto out;
    }

    LOG_DEBUG(log_beep_main, "addrinfo_cb. hostname: %s", hostname);

    inet_ntop(AF_INET,
            (void*) &(((struct sockaddr_in*) address)->sin_addr),
            ip,
            INET_ADDRSTRLEN);

    LOG_DEBUG(log_beep_main, "Resolved ip: %s", ip);

    blob_buf_init(&b, 0);
    blobmsg_add_string(&b, "ip", ip);
    blobmsg_add_u32(&b, "port", resolver_fd->port);
    blobmsg_add_string(&b, "id", resolver_fd->device_id);

    void* r = blobmsg_open_table(&b, "service_info");
    blobmsg_add_string(&b, "service_name", resolver_fd->service_name);
    blobmsg_add_u32(&b, "interface_index", interfaceIndex);
    blobmsg_close_table(&b, r);

    beep_ubus_send_event(ctx, UBUS_EVENT_ADD, b.head);

out:
    uloop_timeout_cancel(&resolver_fd->timeout);
    free(resolver_fd);
}

static void DNSSD_API resolve_cb(DNSServiceRef sdRef, DNSServiceFlags flags,
        uint32_t interfaceIndex, DNSServiceErrorType errorCode,
        const char *fullname, const char *hosttarget, uint16_t netport,
        uint16_t txtLen, const unsigned char *txtRecord, void *context)
{
    struct fd_bridge *resolver_fd = (struct fd_bridge*) context;
    uint16_t port = ntohs(netport);
    int err;

    DNSServiceRefDeallocate(resolver_fd->sd_ref);
    uloop_fd_delete(&resolver_fd->uloop_fd);

    if (errorCode) {
        LOG_ERROR(log_beep_main,
                "resolve_cb received error code: %d", errorCode);
        goto out;
    }

    LOG_DEBUG(log_beep_main, "Resolved host: %s, interface: %d", hosttarget,
            interfaceIndex);

    resolver_fd->port = port;

    // Check that the cluster id matches.

    struct txt_record_t *txt_record =
            tr_parse((const char *)txtRecord, txtLen);

    if(txt_record == NULL) {
        LOG_WARN(log_beep_main, "Failed to parse txt record, ignoring...");
        goto out;
    }

    strncpy(resolver_fd->device_id, tr_get_value(txt_record, "device_id"),
            TR_VAL_MAX_LEN);

    char discovered_cluster_id[TR_VAL_MAX_LEN];
    strncpy(discovered_cluster_id, tr_get_value(txt_record, "cluster_id"),
            TR_VAL_MAX_LEN);

    free(txt_record);

    if(strcmp(discovered_cluster_id, cluster_id)) { // Non-match
        LOG_INFO(log_beep_main, "Ignoring out-of-cluster device %s (%s)",
                resolver_fd->device_id, discovered_cluster_id);
        goto out;
    }

    // not sure if we need to zero sd_ref, but we're going to re-initialize
    // it here, so let's do it just in case.
    memset(&resolver_fd->sd_ref, 0, sizeof(DNSServiceRef));
    err = DNSServiceGetAddrInfo(
            &resolver_fd->sd_ref, 0, interfaceIndex,
            kDNSServiceProtocol_IPv4, hosttarget, addrinfo_cb,
            resolver_fd);
    if (err) {
        LOG_ERROR(log_beep_main, "Failed to GetAddrInfo %s", hosttarget);
        goto out;
    }

    resolver_fd->uloop_fd.fd = DNSServiceRefSockFD(resolver_fd->sd_ref);

    if(resolver_fd->uloop_fd.fd < 0) { // Error
        LOG_ERROR(log_beep_main, "DNSServiceRefSockFD failed in resolve_cb");
        abort();
    }

    resolver_fd->uloop_fd.cb = uloop_mdns_cb;

    uloop_fd_add(&resolver_fd->uloop_fd, ULOOP_READ);
    return;

out:
    uloop_timeout_cancel(&resolver_fd->timeout);
    free(resolver_fd);
}

static void resolve_timeout_cb(struct uloop_timeout *t) {
    struct fd_bridge *resolver_fd = container_of(t, struct fd_bridge, timeout);
    LOG_INFO(log_beep_main, "resolve timeout for: %s",
            resolver_fd->service_name);
    uloop_fd_delete(&resolver_fd->uloop_fd);
    DNSServiceRefDeallocate(resolver_fd->sd_ref);
    free(resolver_fd);
}

static void DNSSD_API browse_cb(DNSServiceRef sdRef, DNSServiceFlags flags,
        uint32_t interfaceIndex, DNSServiceErrorType errorCode,
        const char *serviceName, const char *regType, const char *replyDomain,
        void *context ) {
    if (errorCode) {
        LOG_ERROR(log_beep_main,
                "browse_cb received error code: %d", errorCode);
    } else if(flags & kDNSServiceFlagsAdd) {
        int err;

        LOG_DEBUG(log_beep_main, "browse_cb serviceName: %s interfaceIndex: %d regType: %s replyDomain: %s", serviceName, interfaceIndex, regType, replyDomain);

        struct fd_bridge *resolver_fd = calloc(1, sizeof(struct fd_bridge));
        strncpy(resolver_fd->service_name, serviceName, SERVICE_NAME_MAX_LEN);

        err = DNSServiceResolve(&resolver_fd->sd_ref, 0, interfaceIndex, serviceName,
                regType, replyDomain, resolve_cb, (void*) resolver_fd);
        if(err) {
            LOG_ERROR(log_beep_main, "Failed to resolve %s", serviceName);
            free(resolver_fd);

            return;
        }

        resolver_fd->uloop_fd.fd = DNSServiceRefSockFD(resolver_fd->sd_ref);
        if(resolver_fd->uloop_fd.fd < 0) { // Error
            LOG_ERROR(log_beep_main, "DNSServiceRefSockFD failed in browse_cb");
            abort();
        }

        resolver_fd->uloop_fd.cb = uloop_mdns_cb;
        uloop_fd_add(&resolver_fd->uloop_fd, ULOOP_READ);

        resolver_fd->timeout.cb = &resolve_timeout_cb;
        uloop_timeout_set(&resolver_fd->timeout, RESOLVE_AND_LOOKUP_TIMEOUT);
    } else {
        LOG_DEBUG(log_beep_main, "Service Removed? %s:%s:%s",
                serviceName, regType, replyDomain);

        blob_buf_init(&b, 0);

        void* r = blobmsg_open_table(&b, "service_info");
        blobmsg_add_string(&b, "service_name", serviceName);
        blobmsg_add_u32(&b, "interface_index", interfaceIndex);
        blobmsg_close_table(&b, r);

        beep_ubus_send_event(ctx, UBUS_EVENT_DEL, b.head);
    }
}

static void DNSSD_API query_cb(
        DNSServiceRef sdRef,
        DNSServiceFlags flags,
        uint32_t interfaceIndex,
        DNSServiceErrorType errorCode,
        const char *fullname,
        uint16_t rrtype,
        uint16_t rrclass,
        uint16_t rdlen,
        const void *rdata,
        uint32_t ttl,
        void *context)
{
    struct fd_bridge *query_bridge = (struct fd_bridge *)context;

    LOG_DEBUG(log_beep_main,
            "Query for reconfirm returned for %s with code %d and flags %d", fullname, errorCode, flags);

    DNSServiceErrorType err;

    err = DNSServiceReconfirmRecord(
            kDNSServiceFlagsForce,
            interfaceIndex,
            fullname,
            rrtype,
            rrclass,
            rdlen,
            rdata);
    if(err != kDNSServiceErr_NoError) {
        LOG_WARN(log_beep_main,
                "ReconfirmRecord returned error: %d", err);
    }

    if (flags == kDNSServiceFlagsMoreComing) {
        LOG_INFO(log_beep_main, "Since flags is MoreComing, we\'re not deallocating "
                "the current query");
        return;
    }
    DNSServiceRefDeallocate(query_bridge->sd_ref);
    close(query_bridge->uloop_fd.fd);
    uloop_fd_delete(&query_bridge->uloop_fd);
    free(query_bridge);
}

static void do_reconfirm(char *service_name) {
    char fullname[kDNSServiceMaxDomainName];
    DNSServiceConstructFullName(fullname, service_name, "_beepcontrol._tcp",
            "local.");

    LOG_INFO(log_beep_main, "Reconfirming record for %s", service_name);

    struct fd_bridge *query_bridge = calloc(1, sizeof(struct fd_bridge));

    DNSServiceErrorType err;
    err = DNSServiceQueryRecord(&query_bridge->sd_ref, 0, 0, fullname, kDNSServiceType_SRV,
            kDNSServiceClass_IN, &query_cb, query_bridge);
    if(err != kDNSServiceErr_NoError) {
        LOG_ERROR(log_beep_main, "Couldn't query record for %s", service_name);
        abort();
    }

    query_bridge->uloop_fd.fd = DNSServiceRefSockFD(query_bridge->sd_ref);
    query_bridge->uloop_fd.cb = uloop_mdns_cb;

    uloop_fd_add(&query_bridge->uloop_fd, ULOOP_READ);
}

/*
 * Set up the mDNS-SD "browser" and attach its FD to uloop.  Also, connect to
 * ubus.
 */

static int begin_browsing(void) {
    DNSServiceErrorType err;

    if(browse_fd.browsing) {
        DNSServiceRefDeallocate(browse_fd.sd_ref);
        close(browse_fd.uloop_fd.fd);
        uloop_fd_delete(&browse_fd.uloop_fd);
        browse_fd.browsing = false;
    }

    err = DNSServiceBrowse(
            &browse_fd.sd_ref, 0, 0, "_beepcontrol._tcp", NULL,
            browse_cb, NULL);
    if (err) {
        LOG_ERROR(log_beep_main,
                "Failed to begin browsing, error code = %d", err);
        return 1;
    }

    browse_fd.browsing = true;

    browse_fd.uloop_fd.fd = DNSServiceRefSockFD(browse_fd.sd_ref);
    uloop_fd_add(&browse_fd.uloop_fd, ULOOP_READ);
    return 0;
}

enum {
    RECONFIRM_INFO,
    __RECONFIRM_MAX
};

const struct blobmsg_policy reconfirm_policy[] = {
    [RECONFIRM_INFO] = { .name = "service_info", .type = BLOBMSG_TYPE_TABLE },
};

enum {
    SERVICE_INFO_NAME,
    SERVICE_INFO_INTERFACE_INDEX,
    __SERVICE_INFO_MAX
};

const struct blobmsg_policy service_info_policy[] = {
    [SERVICE_INFO_NAME] = { .name = "service_name", .type = BLOBMSG_TYPE_STRING },
    [SERVICE_INFO_INTERFACE_INDEX] = { .name = "interface_index",
                                       .type = BLOBMSG_TYPE_INT32 }
};

static int reconfirm(struct ubus_context *ctx, struct ubus_object *obj,
        struct ubus_request_data *req, const char *method,
        struct blob_attr *msg) {
    struct blob_attr *tb[__RECONFIRM_MAX];
    struct blob_attr *service_info[__SERVICE_INFO_MAX];
    char *service_name;

    blobmsg_parse(reconfirm_policy,
            ARRAY_SIZE(reconfirm_policy), tb, blob_data(msg), blob_len(msg));

    if (!tb[RECONFIRM_INFO]) {
        beep_reply_error(ctx, req, "Missing info", BEEP_UBUS_ERROR_ARGS);
        return 0;
    }

    blobmsg_parse(service_info_policy,
            ARRAY_SIZE(service_info_policy), service_info,
            blobmsg_data(tb[SERVICE_INFO_NAME]),
            blobmsg_data_len(tb[SERVICE_INFO_NAME]));

    if (!service_info[SERVICE_INFO_NAME]) {
        beep_reply_error(ctx, req, "Missing service info name", BEEP_UBUS_ERROR_ARGS);
        return 0;
    }

    service_name = blobmsg_get_string(service_info[SERVICE_INFO_NAME]);

    do_reconfirm(service_name);

    beep_reply_success(ctx, req, NULL);

    return 0;
}

static int is_alive(struct ubus_context *ctx,
        struct ubus_object *obj, struct ubus_request_data *req,
        const char *method, struct blob_attr *msg) {
    beep_reply_success(ctx, req, NULL);
    return 0;
}

static const struct ubus_method discovery_methods[] = {
    UBUS_METHOD("reconfirm", reconfirm, reconfirm_policy),
    UBUS_METHOD_NOARG("is_alive", is_alive)
};

struct ubus_object_type discovery_type =
    UBUS_OBJECT_TYPE(NULL, discovery_methods);

struct ubus_object discovery_object = {
    .name = "beep.discovery",
    .type = &discovery_type,
    .methods = discovery_methods,
    .n_methods = ARRAY_SIZE(discovery_methods)
};

int main(int argc, char *argv[])
{
    log_beep_main = LOG_CATEGORY_GET("beepdiscovery");
    log_category_set_priority(log_beep_main, LOG_PRIORITY_DEBUG);

    beep_flags_init(argc, argv);

    int ret = beep_config_device_read("device_id", local_id, 100);
    if (ret == -1) {
        LOG_ERROR(log_beep_main,
                "Couldn\'t read device_id from beep config");
        exit(1);
    }
    if (strlen(local_id) > BEEP_UBUS_DEV_ID_MAX_LENGTH) {
        LOG_ERROR(log_beep_main,
                "Device id too long: %s. (max length %d)",
                local_id,
                BEEP_UBUS_DEV_ID_MAX_LENGTH);
        exit(1);
    }

    ret = beep_config_data_read("cluster_id", cluster_id, 100);
    if(ret == -1) {
        LOG_WARN(log_beep_main, "Cluster ID not found; setting default of 0");
        strncpy(cluster_id, "0", 100);
    }

    ret = beep_config_data_read("device_name", friendly_name, 100);
    if(ret == -1) {
        LOG_WARN(log_beep_main, "Device name not found; using default 'unnamed'");
        strncpy(friendly_name, "unnamed", 100);
    }

    uloop_init();

    DNSServiceErrorType err;

    browse_fd.uloop_fd.cb = uloop_mdns_cb;
    browse_fd.uloop_fd.fd = -1;

    if(begin_browsing()) {
        LOG_ERROR(log_beep_main, "begin_browsing() failed.  Exiting.");
        abort();
    }

    ctx = beep_ubus_connect("beepdiscovery");
    if(!ctx) {
        LOG_ERROR(log_beep_main, "Failed to connect to ubus");
        return 1;
    }

    ubus_add_uloop(ctx);

    ret = ubus_add_object(ctx, &discovery_object);
    if(ret) {
        LOG_ERROR(log_beep_main, "Failed to add ubus object: %s",
                ubus_strerror(ret));
        return -1;
    } else {
        LOG_INFO(log_beep_main, "ubus object added.");
    }

    struct txt_record_t txt_record;
    memset(&txt_record, 0, sizeof(txt_record));
    tr_add_entry(&txt_record, DEVICE_ID, local_id);
    tr_add_entry(&txt_record, CLUSTER_ID, cluster_id);
    tr_add_entry(&txt_record, FRIENDLY_NAME, friendly_name);
    if(beepdiscovery_flags.virtual) {
        tr_add_entry(&txt_record, VIRTUAL, "true");
    }

    char *txt_record_str = tr_to_str(&txt_record);

    // TODO: use the callback facility so we can check for async errors.
    err = DNSServiceRegister(
        &sdRefControlService,
        0,                       // flags
        0,                       // interfaceIndex
        local_id,                // name
        "_beepcontrol._tcp",
        NULL,                    // domain
        NULL,                    // host
        htons(beepdiscovery_flags.control_port),   // port
        strlen(txt_record_str),  // txtLen
        txt_record_str,          // txtRecord
        NULL,                    // callBack
        NULL                     // context
        );

    if (err) {
        LOG_ERROR(log_beep_main,
                "Failed to register control service, error code = %d", err);
        return 1;
    }

    err = DNSServiceRegister(
        &sdRefHeadService,
        0,                       // flags
        0,                       // interfaceIndex
        local_id,                // name
        "_beephttp._tcp",
        NULL,                    // domain
        NULL,                    // host
        htons(beepdiscovery_flags.uhttpd_port),   // port
        strlen(txt_record_str),  // txtLen
        txt_record_str,          // txtRecord
        NULL,                    // callBack
        NULL                     // context
        );

    if (err) {
        LOG_ERROR(log_beep_main,
                "Failed to register http service, error code = %d", err);
        return 1;
    }

    uloop_run();

    uloop_done();

    return 0;
}
