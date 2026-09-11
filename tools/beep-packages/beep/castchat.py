"""
Castchat objects
"""

import threading as _threading
import websocket as _websocket
import Queue as _Queue
import re as _re

_hello_msg_fmt = "CASTCHAT/1.0\n" \
       "Content-Length: 0\n" \
       "Namespace: castchat.org:control\n" \
       "Type: Hello\n" \
       "User-Agent: $USERAGENT\n" \
       "\n"

def hello_msg(user_agent):
    return _hello_msg_fmt.replace('$USERAGENT', user_agent)

_msg_fmt = "CASTCHAT/1.0\n" \
        "Namespace: $NAMESPACE\n" \
        "Content-Length: $CONTENT_LENGTH\n" \
        "\n" \
        "$CONTENT\n" \
        "\n"

def msg(namespace, content):
    return _msg_fmt \
            .replace('$NAMESPACE', namespace) \
            .replace('$CONTENT_LENGTH', str(len(content))) \
            .replace('$CONTENT', content)

_msg_reply_pattern_str = \
        "^CASTCHAT/1.0\n" \
        "Content-Length: (?P<content_length>\d*)\n" \
        "Namespace: (?P<namespace>.*)\n" \
        "\n" \
        "(?P<content>.*)\n" \
        "\n"

_msg_reply_pattern = _re.compile(_msg_reply_pattern_str, _re.MULTILINE)

def parse_msg(msg):
    match = _msg_reply_pattern.match(msg)
    if match:
        return match.groupdict()
    else:
        raise ValueError('Invalid CastChat message')

class CastChatClient(object):
    def _on_message(self, ws, message):
        self._messages.put(("message",message))

    def _on_error(self, ws, error):
        self._messages.put(("error",error))

    def _on_open(self, ws):
        self._opened.set()
        self._messages.put(("open",None))

    def _on_close(self, ws):
        self._opened.clear()
        self._closed.set()
        self._messages.put(("close",None))

    def __ws_loop(self):
        self.websocket.run_forever()

    def __init__(self, base, app):
        self.url = "%s%s%s" % \
                (base, '/' if not base.endswith('/') else '', app)

        self._opened = _threading.Event()
        self._closed = _threading.Event()

        self.websocket = _websocket.WebSocketApp(self.url,
                on_message = self._on_message,
                on_error = self._on_error,
                on_open = self._on_open,
                on_close = self._on_close)

        self._messages = _Queue.Queue()

    def open(self):
        self._ws_thread = _threading.Thread(target=self.__ws_loop)
        self._ws_thread.daemon = True
        self._ws_thread.start()
        return self._opened.wait(10)

    def close(self):
        self.websocket.close()
        return self._closed.wait(10)

    def hello(self, user_agent='CastChatClient'):
        """Send a hello message with given user_agent."""
        if self._opened.wait(10) == False:
            raise ValueError('Timed out waiting for websocket')
        self.websocket.send(hello_msg(user_agent))

    def send(self, namespace, content):
        """Send a message.  Does not protect against invalid state, i.e., sending a message
        before sending hello"""

        if not self._opened.is_set() or self._closed.is_set():
            raise ValueError('Websocket is closed')
            return

        self.websocket.send(msg(namespace, content))

    def recv(self, block=True, timeout=10):
        try:
            return self._messages.get(block, timeout)
        except _Queue.Empty:
            return None
