---
title: "Errors"
date: 2020-10-31T17:22:34-04:00
---

The native `panic` function can be used to halt execution in Oba. It accepts a
single argument. When called, the argument is printed along with a stack trace:

```
panic("error")
```

