#include <pcap.h>
#include <stdbool.h>
#include <stdio.h>

#include "extract.h"
#include "ieee802_11.h"

int total_packets = 0;


static void
data_header_print(u_int16_t fc, const u_char *p, const u_int8_t **srcp,
    const u_int8_t **dstp)
{
	u_int subtype = FC_SUBTYPE(fc);

	//if (DATA_FRAME_IS_CF_ACK(subtype) || DATA_FRAME_IS_CF_POLL(subtype) ||
	//    DATA_FRAME_IS_QOS(subtype)) {
	//	printf("CF ");
	//	if (DATA_FRAME_IS_CF_ACK(subtype)) {
	//		if (DATA_FRAME_IS_CF_POLL(subtype))
	//			printf("Ack/Poll");
	//		else
	//			printf("Ack");
	//	} else {
	//		if (DATA_FRAME_IS_CF_POLL(subtype))
	//			printf("Poll");
	//	}
	//	if (DATA_FRAME_IS_QOS(subtype))
	//		printf("+QoS");
	//	printf(" ");
	//}

#define ADDR1  (p + 4)
#define ADDR2  (p + 10)
#define ADDR3  (p + 16)
#define ADDR4  (p + 24)

	if (!FC_TO_DS(fc) && !FC_FROM_DS(fc)) {
		if (srcp != NULL)
			*srcp = ADDR2;
		if (dstp != NULL)
			*dstp = ADDR1;
	} else if (!FC_TO_DS(fc) && FC_FROM_DS(fc)) {
		if (srcp != NULL)
			*srcp = ADDR3;
		if (dstp != NULL)
			*dstp = ADDR1;
	} else if (FC_TO_DS(fc) && !FC_FROM_DS(fc)) {
		if (srcp != NULL)
			*srcp = ADDR2;
		if (dstp != NULL)
			*dstp = ADDR3;
	} else if (FC_TO_DS(fc) && FC_FROM_DS(fc)) {
		if (srcp != NULL)
			*srcp = ADDR4;
		if (dstp != NULL)
			*dstp = ADDR3;
	}

#undef ADDR1
#undef ADDR2
#undef ADDR3
#undef ADDR4
}

void mactostr(const u_int8_t *mac) {
    printf("%02x:%02x:%02x:%02x:%02x:%02x",
           (unsigned int) mac[0],
           (unsigned int) mac[1],
           (unsigned int) mac[2],
           (unsigned int) mac[3],
           (unsigned int) mac[4],
           (unsigned int) mac[5]);
}

int parse_packet(const u_char *p, const struct pcap_pkthdr *header) {
	u_int16_t fc;
	const u_int8_t *src, *dst;

	if (header->caplen < IEEE802_11_FC_LEN) {
        return -1;
    }

	fc = EXTRACT_LE_16BITS(p);
    //printf("%dh\n", FC_TYPE(fc));
    if (FC_TYPE(fc) == T_DATA) {
        data_header_print(fc, p, &src, &dst);
        printf("%d.%d %d %d ", (unsigned int) header->ts.tv_sec,
                               (unsigned int) header->ts.tv_usec,
                               total_packets,
                               header->len);
        mactostr(src);
        printf(" ");
        mactostr(dst);
        printf("\n");
        fflush(stdout);
    }

    return 0;
}

void got_packet(u_char *args, const struct pcap_pkthdr *header,
                const u_char *packet) {
    //printf("PACKET %d len: %d\n", total_packets, header->len);
    parse_packet(packet, header);
    total_packets++;
}

int main(int argc, char *argv[])
{
    pcap_t *handle;         /* Session handle */
    char *dev;          /* The device to sniff on */
    char errbuf[PCAP_ERRBUF_SIZE];  /* Error string */
    struct bpf_program fp;      /* The compiled filter */
    char filter_exp[] = "port 23";  /* The filter expression */
    bpf_u_int32 mask;       /* Our netmask */
    bpf_u_int32 net;        /* Our IP */
    struct pcap_pkthdr header;  /* The header that pcap gives us */
    const u_char *packet;       /* The actual packet */

    /* Define the device */
    //dev = pcap_lookupdev(errbuf);
    dev = "en1";
    if (dev == NULL) {
        fprintf(stderr, "Couldn't find default device: %s\n", errbuf);
        return 1;
    }
    /* Find the properties for the device */
    if (pcap_lookupnet(dev, &net, &mask, errbuf) == -1) {
        fprintf(stderr, "Couldn't get netmask for device %s: %s\n", dev, errbuf);
        net = 0;
        mask = 0;
    }

    /* Open the session in promiscuous mode */
    handle = pcap_create(dev, errbuf);
    if (!handle) {
        fprintf(stderr, "Couldn't open device %s: %s\n", dev, errbuf);
        return 1;
    }

    if (pcap_set_promisc(handle, true) != 0) {
        fprintf(stderr, "pcap_set_promisc failed\n");
        return 1;
    }

    if (pcap_set_rfmon(handle, true) != 0) {
        fprintf(stderr, "pcap_set_rfmon failed: %s\n", pcap_geterr(handle));
        return 1;
    }

    if (pcap_set_buffer_size(handle, BUFSIZ) != 0) {
        fprintf(stderr, "pcap_set_buffer_size failed: %s\n", pcap_geterr(handle));
        return 1;
    }

    if (pcap_activate(handle) != 0) {
        fprintf(stderr, "pcap_activate failed: %s\n", pcap_geterr(handle));
        return 1;
    }

    int dlt = pcap_datalink_name_to_val("IEEE802_11");
    if (dlt == -1) {
        fprintf(stderr, "Invalid datalink name\n");
        return 1;
    }

    if (pcap_set_datalink(handle, dlt) != 0) {
        fprintf(stderr, "pcap_set_datalink failed: %s\n", pcap_geterr(handle));
        return 1;
    }

    //handle = pcap_open_live(dev, BUFSIZ, 1, 1000, errbuf);
    //if (handle == NULL) {
    //    fprintf(stderr, "Couldn't open device %s: %s\n", dev, errbuf);
    //    return(2);
    //}

    pcap_loop(handle, -1, got_packet, NULL);
    fprintf(stderr, "pcap_loop stopped: %s\n", pcap_geterr(handle));

    ///* And close the session */
    //pcap_close(handle); return(0);
}
