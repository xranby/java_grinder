/**
 *  Java Grinder
 *  Author: Michael Kohn
 *   Email: mike@mikekohn.net
 *     Web: http://www.mikekohn.net/
 * License: GPL
 *
 * Copyright 2014 by Michael Kohn
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "JavaClass.h"
#include "compile.h"

static int system_out_println(JavaClass *java_class, Generator *generator)
{
  // Not implemented.
  printf("Enter system_out_println\n");

  return 0;
}

int java_lang_system(JavaClass *java_class, Generator *generator, char *function)
{
  if (strncmp("system_out_println", function, sizeof("system_out_println"-1)) == 0)
  {
    return system_out_println(java_class, generator);
  }

  return -1;
}

