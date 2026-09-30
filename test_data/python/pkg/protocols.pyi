# Type stub (.pyi) with no .py twin.
from .util import slugify          # -> pkg/util.py

class Sluggable:
    def slug(self) -> str: ...
