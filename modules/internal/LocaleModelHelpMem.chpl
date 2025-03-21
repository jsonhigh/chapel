/*
 * Copyright 2020-2025 Hewlett Packard Enterprise Development LP
 * Copyright 2004-2019 Cray Inc.
 * Other additional copyright holders may be indicated within.
 *
 * The entirety of this work is licensed under the Apache License,
 * Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License.
 *
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// LocaleModelHelpMem.chpl
//
// Provides for declarations common to locale model memory management
// but that do not have to be the same in order to meet the
// interface.

// They are in this file as a practical matter to avoid code
// duplication. If necessary, a locale model using this file
// should feel free to reimplement them in some other way.
module LocaleModelHelpMem {
  private use ChapelStandard, CTypes;

  //////////////////////////////////////////
  //
  // support for memory management
  //

  pragma "fn synchronization free"
  private extern proc chpl_memhook_md_num(): chpl_mem_descInt_t;

  // The allocator pragma is used by scalar replacement.

  // Note that there are 2 nearly identical chpl_here_alloc() functions. This
  // one takes an int(64) size and is marked with "locale model alloc" while
  // the second version takes a generic `integral` size and is not marked
  // "locale model alloc". Calls to the "locale model alloc" version are
  // inserted by the compiler (sometimes after resolution) for class/record
  // allocations. As a result, there can only be a single function with "locale
  // model alloc" in any compilation and the function must be fully specified.
  pragma "allocator"
  pragma "locale model alloc"
  pragma "always propagate line file info"
  proc chpl_here_alloc(size:int(64), md:chpl_mem_descInt_t): c_ptr(void) {
    pragma "fn synchronization free"
    pragma "insert line file info"
      extern proc chpl_mem_alloc(size:c_size_t, md:chpl_mem_descInt_t) : c_ptr(void);
    return chpl_mem_alloc(size.safeCast(c_size_t), md + chpl_memhook_md_num());
  }

  pragma "allocator"
  pragma "llvm return noalias"
  pragma "always propagate line file info"
  proc chpl_here_alloc(size:integral, md:chpl_mem_descInt_t): c_ptr(void) {
    pragma "fn synchronization free"
    pragma "insert line file info"
      extern proc chpl_mem_alloc(size:c_size_t, md:chpl_mem_descInt_t) : c_ptr(void);
    return chpl_mem_alloc(size.safeCast(c_size_t), md + chpl_memhook_md_num());
  }

  import Allocators;
  pragma "allocator"
  pragma "llvm return noalias"
  pragma "always propagate line file info"
  proc chpl_here_alloc_with_allocator(size:int(64),
                                      md:chpl_mem_descInt_t,
                                      ref allocator: record): c_ptr(void) {
    // TODO: what to do with `md`?
    return allocator.allocate(size);
  }
  pragma "allocator"
  pragma "llvm return noalias"
  pragma "always propagate line file info"
  proc chpl_here_alloc_with_allocator(size:int(64),
                                      md:chpl_mem_descInt_t,
                                      const ref allocator: class): c_ptr(void) {
    // TODO: what to do with `md`?
    return allocator.allocate(size);
  }

  pragma "allocator"
  pragma "llvm return noalias"
  pragma "always propagate line file info"
  proc chpl_here_aligned_alloc(alignment:integral, size:integral,
                               md:chpl_mem_descInt_t): c_ptr(void) {
    pragma "fn synchronization free"
    pragma "insert line file info"
    extern proc chpl_mem_memalign(alignment:c_size_t, size:c_size_t, md:chpl_mem_descInt_t) : c_ptr(void);
    return chpl_mem_memalign(alignment.safeCast(c_size_t),
                             size.safeCast(c_size_t),
                             md + chpl_memhook_md_num());
  }

  pragma "allocator"
  pragma "llvm return noalias"
  pragma "always propagate line file info"
  proc chpl_here_calloc(size:integral, number:integral, md:chpl_mem_descInt_t): c_ptr(void) {
    pragma "fn synchronization free"
    pragma "insert line file info"
      extern proc chpl_mem_calloc(number:c_size_t, size:c_size_t, md:chpl_mem_descInt_t) : c_ptr(void);
    return chpl_mem_calloc(number.safeCast(c_size_t), size.safeCast(c_size_t), md + chpl_memhook_md_num());
  }

  pragma "allocator"
  pragma "always propagate line file info"
  proc chpl_here_realloc(ptr:c_ptr(void), size:integral, md:chpl_mem_descInt_t): c_ptr(void) {
    pragma "fn synchronization free"
    pragma "insert line file info"
      extern proc chpl_mem_realloc(ptr:c_ptr(void), size:c_size_t, md:chpl_mem_descInt_t) : c_ptr(void);
    return chpl_mem_realloc(ptr:c_ptr(void), size.safeCast(c_size_t), md + chpl_memhook_md_num());
  }

  pragma "fn synchronization free"
  pragma "always propagate line file info"
  proc chpl_here_good_alloc_size(min_size:integral): min_size.type {
    pragma "fn synchronization free"
    pragma "insert line file info"
      extern proc chpl_mem_good_alloc_size(min_size:c_size_t) : c_size_t;
    return chpl_mem_good_alloc_size(min_size.safeCast(c_size_t)).safeCast(min_size.type);
  }

  pragma "locale model free"
  pragma "always propagate line file info"
  proc chpl_here_free(ptr:c_ptr(void)): void {
    pragma "fn synchronization free"
    pragma "insert line file info"
      extern proc chpl_mem_free(ptr:c_ptr(void)) : void;
    chpl_mem_free(ptr:c_ptr(void));
  }
}
