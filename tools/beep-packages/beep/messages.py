"""
Messaging interface
"""

import socket, json, threading, select, copy, Queue

def _send(socket, namespace, msg, header=None):
    frame = 'Content-Length: %d\n' % (len(msg),)
    frame += 'Namespace: %s\n' % (namespace,)
    if header:
        for k,v in head.iteritems():
            frame += '%s: %s\n' % (k, v)
    frame += '\n%s\n\n' % msg
    socket.send(frame)

def _consume_message(buf):
    # Assume we begin with a header section
    header_end = buf.find('\n\n')

    if header_end < 0:
        return (None, None, None)

    header_str = buf[0:header_end]
    header_lines = header_str.split('\n')
    headers = {}

    for line in header_lines:
        colon = line.find(':')
        if colon < 0:
            continue
        name = line[0:colon].strip().lower()
        value = line[colon+1:].strip()

        if not name or not value:
            continue

        headers[str(name)] = str(value)

    if not headers['content-length']:
        raise Exception('Invalid message (content-length missing)')

    content_len = int(headers['content-length'])

    if content_len < 1:
        raise Exception('Invalid message (content-length < 1)')

    buf = buf[header_end+2:]
    if len(buf) < content_len + 2:
        return (None, None, None)

    if buf[content_len:content_len+2] != '\n\n':
        raise Exception('Invalid message (bad terminator)')

    message = buf[0:content_len]
    return (headers, message, header_end+content_len+4)

class Messenger(object):
    def __init__(self, host, port):
        """Creates a Messenger object bound to the beepcomm messaging service
        at <host>:<port>

        Attribtures:
            listen_cb       An optional callback that will be called upon
                            receiving each message.  Otherwise, messages will
                            be placed in the messages attribute (a Queue)
        """
        self.socket = None
        self.listen_cb = None
        self.app = None
        self.host = host
        self.port = port
        self.messages = Queue.Queue()
        self._recv_buffer = bytearray()

    def __receive(self):
        while True and self.socket:
            ready = select.select([self.socket], [], [], 1)
            if ready[0]:
                response = self.socket.recv(4096)
                self._recv_buffer += response
                (headers, message, consume_len) = \
                        _consume_message(self._recv_buffer)
                if consume_len:
                    self._recv_buffer = self._recv_buffer[consume_len:]
                    self.messages.put({'headers':headers,'message':message})
                    if self.listen_cb:
                        self.listen_cb(self.messages.get())

    def connect(self, app):
        """Connects to the messaging service and binds to <app>"""

        self.app = app
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.socket.connect((self.host, self.port))
        self._recv_buffer_lock = threading.Lock()
        self._recv_thread = threading.Thread(target=self.__receive)
        self._recv_thread.daemon = True
        self._recv_thread.start()
        _send(self.socket, '__control__',
                json.dumps({
                    'type':'hello',
                    'app':self.app,
                    'user_agent':'beep-messenger'
                    }))

    def disconnect(self):
        if self.socket:
            self.socket.close()
            self.socket = None
            self.app = None

    def send(self, message, namespace='default'):
        if not self.socket:
            raise Exception("Must call connect() before calling send(...)")

        try:
            _send(self.socket, namespace, message)
        except socket.error as e:
            raise Exception("Failed to send message: %s (%d)" % \
                    (e.strerror, e.errno))
            self.socket = None
