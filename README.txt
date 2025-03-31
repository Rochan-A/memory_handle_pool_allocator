HandlePool
----------

Implementation of handles for memory management as described in
https://floooh.github.io/2018/06/17/handles-vs-pointers.html.

Implements an array-like container that uses 'handles' to manage a collection of elements of a
user-defined type. It behaves similar to a memory pool, supporting 'creation' and 'destruction' of
elements without incurring expensive dynamic memory allocation and having callers deal with raw
pointers directly. Handles enables safe memory and reference management, even in the face of
frequent operations.

Handles are unique 64-bit structs that represent 'references' to elements in the container. Each
handle encodes an index and a generation counter, which the HandlePool uses to verify that the
referenced element is still valid and to allow safe modifications of that element.

Since the implementation is based on arrays, it provides better cache performance than alternatives
like std::unordered_map, which will typically scatter its element over the heap. This also provides
more predictable allocation behavior by preallocating using the Reserve method.

Useful if you want to avoid:
- dealing with pointers
- unnecessary dynamic memory allocations
- desire a pool-like container that is simple and fast
