// Rule 1: Replace raw pointer declarations with VPtr
// This handles cases like: int* p = ...; -> VPtr<int> p(...);
@@
type T;
identifier p;
expression E;
@@
- T* p = E;
+ VPtr<T> p(vmm, (uint32_t)E);

// Rule 2: Handle pointer assignments
// p = some_address; -> p = VPtr<T>(vmm, some_address);
@@
type T;
identifier p;
expression E;
@@
  VPtr<T> p;
  ...
- p = E;
+ p = VPtr<T>(vmm, (uint32_t)E);

// Rule 3: Replace dereferences (*p) with the object itself
// Because VPtr has 'operator T()', *p is usually redundant or
// needs to be converted to the object to trigger the access() method.
@@
identifier p;
@@
- *p
+ p

// Rule 4: Replace memset with vmemset
@@
expression PTR, VAL, LEN;
@@
- memset(PTR, VAL, LEN);
+ vmemset(vmm, (uint32_t)PTR, VAL, LEN);

// Rule 5: Replace memcpy with vmemcpy
@@
expression DEST, SRC, LEN;
@@
- memcpy(DEST, SRC, LEN);
+ vmemcpy(vmm, (uint32_t)DEST, (uint32_t)SRC, LEN);