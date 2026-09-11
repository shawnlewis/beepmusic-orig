#ifndef BEEPDATA_H
#define BEEPDATA_H

void beep_data_init();
char* beep_data_on_data_request(
        void* userdata, int subtarget_id, int request_id,
        const uint8_t* args, int args_len, int* response_len);


#endif  // BEEPDATA_H
