# Future tests

Tests that are not yet compilable or are waiting on API
changes live here. They are excluded from the CI run in
`ci/verify.py` because that runner globs only
`tests/**/*.c`.

## Contents

- `test_race_conditions.c` - uses C++ lambda syntax
  (`[](void *arg){...}`) in a `.c` file and references
  `ol_actor_mailbox_len`, which does not exist in the
  current API. To be rewritten as plain C functions during
  v1.3.3.
