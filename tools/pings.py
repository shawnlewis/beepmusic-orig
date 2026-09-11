#!/usr/bin/python
#
# prints all the pings for a given day from production devices.

import copy
import csv
import json
import pprint
import sys
from elasticsearch import Elasticsearch

# 100k is too much, we get a memory error.
size = 25000

query = {
  'query': {
    'filtered': {
      'query': {
        'query_string': {
          'query': 'ping'
        }
      }
    }
  },
  'size': size,
  'from': 0
}

def go():
    es = Elasticsearch()
    from_ = 0
    messages = []
    while True:
        print from_
        query['from'] = from_
        result = es.search(index='logstash-2015.05.20', body=query)
        hits = result['hits']['hits']
        if not hits:
            break
        for hit in hits:
            source = hit['_source']
            message = source['message']
            ping = message.strip().split()[-1]
            messages.append('%s %s %s' %
                    (source['@timestamp'],
                        source['beep_id'],
                        str(ping)))
        from_ += size
    print(len(messages))

    f = open('/tmp/results.txt', 'w')
    f.write('\n'.join(messages))
    f.close()


def main(argv):
    go()

if __name__ == '__main__':
    main(sys.argv)
