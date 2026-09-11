for dev in $@; do
    ssh "$dev" "uci delete beep.main.device_name; uci delete beep.main.playnet_gain; uci delete beep.main.cluster_id; uci delete beep.main.global_volume; uci set beep.main.wifisetup_on_boot=1 ; uci commit ; cat /etc/config/beep"
done
