<?php

/* Leaf helper, no namespace -- required by Program.php via a
 * same-directory, no-slash path (require's bare-filename fallback). */

function format_area(float $area): string
{
    return number_format($area, 2);
}
