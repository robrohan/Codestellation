"""Top-level entry point -- imports into the pkg package a few ways."""
import pkg.core
from pkg import models
from pkg.core import run as _run


def main():
    _run()
    print(models.Widget().describe())


if __name__ == "__main__":
    main()
