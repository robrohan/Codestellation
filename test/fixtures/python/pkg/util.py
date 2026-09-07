"""Leaf module -- depends on nothing in this package."""


def slugify(text):
    return text.strip().lower().replace(" ", "-")
