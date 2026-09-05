#include "foo.h"
#include "bar.h"
#include <stdio.h>

int main(void) {
    foo_hello();
    printf("%d\n", bar_add(2, 3));
    return 0;
}
