"""
Dial objects
"""

import threading as _threading, socket as _socket, \
        select as _select, Queue as _Queue, time as _time, \
        requests as _requests, sets as _sets

def _xml_extract(s, attr_name):
    attr = '<%s>' % attr_name.lower()
    start = s.lower().find(attr)
    end = s.lower().find('</%s>' % attr_name.lower())
    if (start < 0) or (end < 0) or (end < start):
        return ""

    return s[start + len(attr) : end]

class DialDiscovery(object):
    def _parse_ssdp_response(self, response):
        url = None
        usn = None
        lines = response.split('\n')
        for line in lines:
            if line.lower().startswith('location:'):
                url = line.split(' ')[1].strip()
            elif line.lower().startswith('usn:'):
                usn = line.split(' ')[1].strip()
        return (url, usn)

    def __ssdp_listen(self):
        self._socket = _socket.socket(_socket.AF_INET, _socket.SOCK_DGRAM)
        self._socket.setblocking(0)
        self._socket.bind(("0.0.0.0",0))

        self._socket_bound.set()

        while not self._shutdown.is_set():
            ready = _select.select([self._socket], [], [], 1)
            if ready[0]:
                response = self._socket.recv(4096)
                url, usn = self._parse_ssdp_response(response)
                if self._debug:
                    print 'SSDP Response =>', response

                ddxml_req = _requests.get(url)

                if self._debug:
                    print 'dd.xml =>', ddxml_req.text

                friendly_name = _xml_extract(ddxml_req.text,'friendlyName')
                manufacturer = _xml_extract(ddxml_req.text,'manufacturer')
                model_name = _xml_extract(ddxml_req.text,'modelName')
                udn = _xml_extract(ddxml_req.text,'UDN')

                try:
                    dial_url = ddxml_req.headers['application-url']
                    self._ssdp_responses.put({
                            'url':dial_url,
                            'friendly_name':friendly_name,
                            'manufacturer':manufacturer,
                            'model_name':model_name,
                            'udn':udn})
                except KeyError:
                    pass
        self._socket.close()

    def __init__(self, debug=False):
        self._debug = debug
        self._ssdp_responses = _Queue.Queue()
        self._shutdown = _threading.Event()
        self._socket_bound = _threading.Event()
        self._listen_thread = None

    def __del__(self):
        self.stop()

    def __start(self, num):
        self._listen_thread = _threading.Thread(target=self.__ssdp_listen)
        self._listen_thread.daemon = True
        self._listen_thread.start()

        if not self._socket_bound.wait(3):
            raise IOError('Failed to bind socket')

        reqbody = \
                "M-SEARCH * HTTP/1.1\n" \
                "HOST: 239.255.255.250:1900\n" \
                "MAN: \"ssdp:discover\"\n" \
                "MX: 5\n" \
                "ST: urn:dial-multiscreen-org:service:dial:1\n" \
                "USER-AGENT: linux/2.6 py-dial-client/1.0\n"
        for i in range(num):
            _time.sleep(0.2)
            self._socket.sendto(reqbody, 0, ("239.255.255.250",1900))

    def start(self,num=3,async=True):
        self._shutdown.clear()
        if async:
            _threading.Thread(target=self.__start,args=(num,)).start()
        else:
            self.__start(num)

    def get(self, *args, **kwargs):
        if len(args) < 1:
            args = (False,)
        return self._ssdp_responses.get(*args, **kwargs)

    def stop(self):
        self._shutdown.set()
        if self._listen_thread:
            self._listen_thread.join()
            self._listen_thread = None

    def discover_all(self, timeout=3):
        self.start(async=False)
        _time.sleep(timeout)
        self.stop()
        responses = []
        while True:
            try:
                r = self.get()
                if len(filter(lambda o:o['udn'] == r['udn'], responses)) == 0:
                    responses.append(r)
            except _Queue.Empty:
                break
        return responses

class DialInterface(object):
    """Interface to a DIAL service at the given URL.  Use DialDiscovery to
    find available URLs"""

    def __init__(self, url):
        self.url = url

    def info(self, app):
        req = _requests.get("%s/%s" % (self.url, app))
        return (req.status_code, req.text)

    def start(self, app, args=None):
        req = _requests.post("%s/%s" % (self.url, app), data=args)
        return (req.status_code,
                req.headers['location'] if 'location' in req.headers else '')

    def stop(self, app_instance):
        req = _requests.delete("%s/%s" % (self.url, app_instance))
        return (req.status_code, req.text)
