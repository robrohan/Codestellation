"""Domain types -- uses util via an absolute intra-package import."""
from pkg.util import slugify


class Widget:
    def describe(self):
        return slugify("A Widget")
