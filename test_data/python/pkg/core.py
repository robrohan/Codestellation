"""Ties the package together -- one relative import, one absolute."""
from .util import slugify
from . import models


def run():
    w = models.Widget()
    return slugify(w.describe())
