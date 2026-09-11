from beep.utils import quick_cmd

# This parsing is tested on binutils 2.24.

def list_sections(path):
    sec_hdr = quick_cmd('readelf -S -W {}'.format(path))
    sec_hdr = sec_hdr.split('\n')
    sec_hdr = sec_hdr[4:-4]
    sec_list = [x.split(']')[1].lstrip().split(' ')[0] for x in sec_hdr]
    return sec_list

def del_section(path, section):
    quick_cmd('objcopy -R {} {}'.format(section, path))
