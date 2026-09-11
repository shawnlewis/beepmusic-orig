import os

_all_files = os.listdir(os.path.dirname(__file__))
_py_files = filter(lambda x:x.startswith('test_') and x.endswith('.py'), _all_files)

__all__ = ['core'] + [pyfile.replace('.py','') for pyfile in _py_files]
