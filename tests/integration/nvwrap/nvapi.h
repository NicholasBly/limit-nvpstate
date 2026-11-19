#pragma once
// mingw wrapper: NVAPI expects MSVC SAL macros; define them only around nvapi.h
#ifndef __ecount
#define __ecount(size)
#define WRAP_UNDEF__ecount
#endif
#ifndef __bcount
#define __bcount(size)
#define WRAP_UNDEF__bcount
#endif
#ifndef __in
#define __in
#define WRAP_UNDEF__in
#endif
#ifndef __in_ecount
#define __in_ecount(size)
#define WRAP_UNDEF__in_ecount
#endif
#ifndef __in_bcount
#define __in_bcount(size)
#define WRAP_UNDEF__in_bcount
#endif
#ifndef __in_z
#define __in_z
#define WRAP_UNDEF__in_z
#endif
#ifndef __in_ecount_z
#define __in_ecount_z(size)
#define WRAP_UNDEF__in_ecount_z
#endif
#ifndef __in_bcount_z
#define __in_bcount_z(size)
#define WRAP_UNDEF__in_bcount_z
#endif
#ifndef __in_nz
#define __in_nz
#define WRAP_UNDEF__in_nz
#endif
#ifndef __in_ecount_nz
#define __in_ecount_nz(size)
#define WRAP_UNDEF__in_ecount_nz
#endif
#ifndef __in_bcount_nz
#define __in_bcount_nz(size)
#define WRAP_UNDEF__in_bcount_nz
#endif
#ifndef __out
#define __out
#define WRAP_UNDEF__out
#endif
#ifndef __out_ecount
#define __out_ecount(size)
#define WRAP_UNDEF__out_ecount
#endif
#ifndef __out_bcount
#define __out_bcount(size)
#define WRAP_UNDEF__out_bcount
#endif
#ifndef __out_ecount_part
#define __out_ecount_part(size,length)
#define WRAP_UNDEF__out_ecount_part
#endif
#ifndef __out_bcount_part
#define __out_bcount_part(size,length)
#define WRAP_UNDEF__out_bcount_part
#endif
#ifndef __out_ecount_full
#define __out_ecount_full(size)
#define WRAP_UNDEF__out_ecount_full
#endif
#ifndef __out_bcount_full
#define __out_bcount_full(size)
#define WRAP_UNDEF__out_bcount_full
#endif
#ifndef __out_z
#define __out_z
#define WRAP_UNDEF__out_z
#endif
#ifndef __out_z_opt
#define __out_z_opt
#define WRAP_UNDEF__out_z_opt
#endif
#ifndef __out_ecount_z
#define __out_ecount_z(size)
#define WRAP_UNDEF__out_ecount_z
#endif
#ifndef __out_bcount_z
#define __out_bcount_z(size)
#define WRAP_UNDEF__out_bcount_z
#endif
#ifndef __out_ecount_part_z
#define __out_ecount_part_z(size,length)
#define WRAP_UNDEF__out_ecount_part_z
#endif
#ifndef __out_bcount_part_z
#define __out_bcount_part_z(size,length)
#define WRAP_UNDEF__out_bcount_part_z
#endif
#ifndef __out_ecount_full_z
#define __out_ecount_full_z(size)
#define WRAP_UNDEF__out_ecount_full_z
#endif
#ifndef __out_bcount_full_z
#define __out_bcount_full_z(size)
#define WRAP_UNDEF__out_bcount_full_z
#endif
#ifndef __out_nz
#define __out_nz
#define WRAP_UNDEF__out_nz
#endif
#ifndef __out_nz_opt
#define __out_nz_opt
#define WRAP_UNDEF__out_nz_opt
#endif
#ifndef __out_ecount_nz
#define __out_ecount_nz(size)
#define WRAP_UNDEF__out_ecount_nz
#endif
#ifndef __out_bcount_nz
#define __out_bcount_nz(size)
#define WRAP_UNDEF__out_bcount_nz
#endif
#ifndef __inout
#define __inout
#define WRAP_UNDEF__inout
#endif
#ifndef __inout_ecount
#define __inout_ecount(size)
#define WRAP_UNDEF__inout_ecount
#endif
#ifndef __inout_bcount
#define __inout_bcount(size)
#define WRAP_UNDEF__inout_bcount
#endif
#ifndef __inout_ecount_part
#define __inout_ecount_part(size,length)
#define WRAP_UNDEF__inout_ecount_part
#endif
#ifndef __inout_bcount_part
#define __inout_bcount_part(size,length)
#define WRAP_UNDEF__inout_bcount_part
#endif
#ifndef __inout_ecount_full
#define __inout_ecount_full(size)
#define WRAP_UNDEF__inout_ecount_full
#endif
#ifndef __inout_bcount_full
#define __inout_bcount_full(size)
#define WRAP_UNDEF__inout_bcount_full
#endif
#ifndef __inout_z
#define __inout_z
#define WRAP_UNDEF__inout_z
#endif
#ifndef __inout_ecount_z
#define __inout_ecount_z(size)
#define WRAP_UNDEF__inout_ecount_z
#endif
#ifndef __inout_bcount_z
#define __inout_bcount_z(size)
#define WRAP_UNDEF__inout_bcount_z
#endif
#ifndef __inout_nz
#define __inout_nz
#define WRAP_UNDEF__inout_nz
#endif
#ifndef __inout_ecount_nz
#define __inout_ecount_nz(size)
#define WRAP_UNDEF__inout_ecount_nz
#endif
#ifndef __inout_bcount_nz
#define __inout_bcount_nz(size)
#define WRAP_UNDEF__inout_bcount_nz
#endif
#ifndef __ecount_opt
#define __ecount_opt(size)
#define WRAP_UNDEF__ecount_opt
#endif
#ifndef __bcount_opt
#define __bcount_opt(size)
#define WRAP_UNDEF__bcount_opt
#endif
#ifndef __in_opt
#define __in_opt
#define WRAP_UNDEF__in_opt
#endif
#ifndef __in_ecount_opt
#define __in_ecount_opt(size)
#define WRAP_UNDEF__in_ecount_opt
#endif
#ifndef __in_bcount_opt
#define __in_bcount_opt(size)
#define WRAP_UNDEF__in_bcount_opt
#endif
#ifndef __in_z_opt
#define __in_z_opt
#define WRAP_UNDEF__in_z_opt
#endif
#ifndef __in_ecount_z_opt
#define __in_ecount_z_opt(size)
#define WRAP_UNDEF__in_ecount_z_opt
#endif
#ifndef __in_bcount_z_opt
#define __in_bcount_z_opt(size)
#define WRAP_UNDEF__in_bcount_z_opt
#endif
#ifndef __in_nz_opt
#define __in_nz_opt
#define WRAP_UNDEF__in_nz_opt
#endif
#ifndef __in_ecount_nz_opt
#define __in_ecount_nz_opt(size)
#define WRAP_UNDEF__in_ecount_nz_opt
#endif
#ifndef __in_bcount_nz_opt
#define __in_bcount_nz_opt(size)
#define WRAP_UNDEF__in_bcount_nz_opt
#endif
#ifndef __out_opt
#define __out_opt
#define WRAP_UNDEF__out_opt
#endif
#ifndef __out_ecount_opt
#define __out_ecount_opt(size)
#define WRAP_UNDEF__out_ecount_opt
#endif
#ifndef __out_bcount_opt
#define __out_bcount_opt(size)
#define WRAP_UNDEF__out_bcount_opt
#endif
#ifndef __out_ecount_part_opt
#define __out_ecount_part_opt(size,length)
#define WRAP_UNDEF__out_ecount_part_opt
#endif
#ifndef __out_bcount_part_opt
#define __out_bcount_part_opt(size,length)
#define WRAP_UNDEF__out_bcount_part_opt
#endif
#ifndef __out_ecount_full_opt
#define __out_ecount_full_opt(size)
#define WRAP_UNDEF__out_ecount_full_opt
#endif
#ifndef __out_bcount_full_opt
#define __out_bcount_full_opt(size)
#define WRAP_UNDEF__out_bcount_full_opt
#endif
#ifndef __out_ecount_z_opt
#define __out_ecount_z_opt(size)
#define WRAP_UNDEF__out_ecount_z_opt
#endif
#ifndef __out_bcount_z_opt
#define __out_bcount_z_opt(size)
#define WRAP_UNDEF__out_bcount_z_opt
#endif
#ifndef __out_ecount_part_z_opt
#define __out_ecount_part_z_opt(size,length)
#define WRAP_UNDEF__out_ecount_part_z_opt
#endif
#ifndef __out_bcount_part_z_opt
#define __out_bcount_part_z_opt(size,length)
#define WRAP_UNDEF__out_bcount_part_z_opt
#endif
#ifndef __out_ecount_full_z_opt
#define __out_ecount_full_z_opt(size)
#define WRAP_UNDEF__out_ecount_full_z_opt
#endif
#ifndef __out_bcount_full_z_opt
#define __out_bcount_full_z_opt(size)
#define WRAP_UNDEF__out_bcount_full_z_opt
#endif
#ifndef __out_ecount_nz_opt
#define __out_ecount_nz_opt(size)
#define WRAP_UNDEF__out_ecount_nz_opt
#endif
#ifndef __out_bcount_nz_opt
#define __out_bcount_nz_opt(size)
#define WRAP_UNDEF__out_bcount_nz_opt
#endif
#ifndef __inout_opt
#define __inout_opt
#define WRAP_UNDEF__inout_opt
#endif
#ifndef __inout_ecount_opt
#define __inout_ecount_opt(size)
#define WRAP_UNDEF__inout_ecount_opt
#endif
#ifndef __inout_bcount_opt
#define __inout_bcount_opt(size)
#define WRAP_UNDEF__inout_bcount_opt
#endif
#ifndef __inout_ecount_part_opt
#define __inout_ecount_part_opt(size,length)
#define WRAP_UNDEF__inout_ecount_part_opt
#endif
#ifndef __inout_bcount_part_opt
#define __inout_bcount_part_opt(size,length)
#define WRAP_UNDEF__inout_bcount_part_opt
#endif
#ifndef __inout_ecount_full_opt
#define __inout_ecount_full_opt(size)
#define WRAP_UNDEF__inout_ecount_full_opt
#endif
#ifndef __inout_bcount_full_opt
#define __inout_bcount_full_opt(size)
#define WRAP_UNDEF__inout_bcount_full_opt
#endif
#ifndef __inout_z_opt
#define __inout_z_opt
#define WRAP_UNDEF__inout_z_opt
#endif
#ifndef __inout_ecount_z_opt
#define __inout_ecount_z_opt(size)
#define WRAP_UNDEF__inout_ecount_z_opt
#endif
#ifndef __inout_ecount_z_opt
#define __inout_ecount_z_opt(size)
#define WRAP_UNDEF__inout_ecount_z_opt
#endif
#ifndef __inout_bcount_z_opt
#define __inout_bcount_z_opt(size)
#define WRAP_UNDEF__inout_bcount_z_opt
#endif
#ifndef __inout_nz_opt
#define __inout_nz_opt
#define WRAP_UNDEF__inout_nz_opt
#endif
#ifndef __inout_ecount_nz_opt
#define __inout_ecount_nz_opt(size)
#define WRAP_UNDEF__inout_ecount_nz_opt
#endif
#ifndef __inout_bcount_nz_opt
#define __inout_bcount_nz_opt(size)
#define WRAP_UNDEF__inout_bcount_nz_opt
#endif
#ifndef __deref_ecount
#define __deref_ecount(size)
#define WRAP_UNDEF__deref_ecount
#endif
#ifndef __deref_bcount
#define __deref_bcount(size)
#define WRAP_UNDEF__deref_bcount
#endif
#ifndef __deref_out
#define __deref_out
#define WRAP_UNDEF__deref_out
#endif
#ifndef __deref_out_ecount
#define __deref_out_ecount(size)
#define WRAP_UNDEF__deref_out_ecount
#endif
#ifndef __deref_out_bcount
#define __deref_out_bcount(size)
#define WRAP_UNDEF__deref_out_bcount
#endif
#ifndef __deref_out_ecount_part
#define __deref_out_ecount_part(size,length)
#define WRAP_UNDEF__deref_out_ecount_part
#endif
#ifndef __deref_out_bcount_part
#define __deref_out_bcount_part(size,length)
#define WRAP_UNDEF__deref_out_bcount_part
#endif
#ifndef __deref_out_ecount_full
#define __deref_out_ecount_full(size)
#define WRAP_UNDEF__deref_out_ecount_full
#endif
#ifndef __deref_out_bcount_full
#define __deref_out_bcount_full(size)
#define WRAP_UNDEF__deref_out_bcount_full
#endif
#ifndef __deref_out_z
#define __deref_out_z
#define WRAP_UNDEF__deref_out_z
#endif
#ifndef __deref_out_ecount_z
#define __deref_out_ecount_z(size)
#define WRAP_UNDEF__deref_out_ecount_z
#endif
#ifndef __deref_out_bcount_z
#define __deref_out_bcount_z(size)
#define WRAP_UNDEF__deref_out_bcount_z
#endif
#ifndef __deref_out_nz
#define __deref_out_nz
#define WRAP_UNDEF__deref_out_nz
#endif
#ifndef __deref_out_ecount_nz
#define __deref_out_ecount_nz(size)
#define WRAP_UNDEF__deref_out_ecount_nz
#endif
#ifndef __deref_out_bcount_nz
#define __deref_out_bcount_nz(size)
#define WRAP_UNDEF__deref_out_bcount_nz
#endif
#ifndef __deref_inout
#define __deref_inout
#define WRAP_UNDEF__deref_inout
#endif
#ifndef __deref_inout_z
#define __deref_inout_z
#define WRAP_UNDEF__deref_inout_z
#endif
#ifndef __deref_inout_ecount
#define __deref_inout_ecount(size)
#define WRAP_UNDEF__deref_inout_ecount
#endif
#ifndef __deref_inout_bcount
#define __deref_inout_bcount(size)
#define WRAP_UNDEF__deref_inout_bcount
#endif
#ifndef __deref_inout_ecount_part
#define __deref_inout_ecount_part(size,length)
#define WRAP_UNDEF__deref_inout_ecount_part
#endif
#ifndef __deref_inout_bcount_part
#define __deref_inout_bcount_part(size,length)
#define WRAP_UNDEF__deref_inout_bcount_part
#endif
#ifndef __deref_inout_ecount_full
#define __deref_inout_ecount_full(size)
#define WRAP_UNDEF__deref_inout_ecount_full
#endif
#ifndef __deref_inout_bcount_full
#define __deref_inout_bcount_full(size)
#define WRAP_UNDEF__deref_inout_bcount_full
#endif
#ifndef __deref_inout_z
#define __deref_inout_z
#define WRAP_UNDEF__deref_inout_z
#endif
#ifndef __deref_inout_ecount_z
#define __deref_inout_ecount_z(size)
#define WRAP_UNDEF__deref_inout_ecount_z
#endif
#ifndef __deref_inout_bcount_z
#define __deref_inout_bcount_z(size)
#define WRAP_UNDEF__deref_inout_bcount_z
#endif
#ifndef __deref_inout_nz
#define __deref_inout_nz
#define WRAP_UNDEF__deref_inout_nz
#endif
#ifndef __deref_inout_ecount_nz
#define __deref_inout_ecount_nz(size)
#define WRAP_UNDEF__deref_inout_ecount_nz
#endif
#ifndef __deref_inout_bcount_nz
#define __deref_inout_bcount_nz(size)
#define WRAP_UNDEF__deref_inout_bcount_nz
#endif
#ifndef __deref_ecount_opt
#define __deref_ecount_opt(size)
#define WRAP_UNDEF__deref_ecount_opt
#endif
#ifndef __deref_bcount_opt
#define __deref_bcount_opt(size)
#define WRAP_UNDEF__deref_bcount_opt
#endif
#ifndef __deref_out_opt
#define __deref_out_opt
#define WRAP_UNDEF__deref_out_opt
#endif
#ifndef __deref_out_ecount_opt
#define __deref_out_ecount_opt(size)
#define WRAP_UNDEF__deref_out_ecount_opt
#endif
#ifndef __deref_out_bcount_opt
#define __deref_out_bcount_opt(size)
#define WRAP_UNDEF__deref_out_bcount_opt
#endif
#ifndef __deref_out_ecount_part_opt
#define __deref_out_ecount_part_opt(size,length)
#define WRAP_UNDEF__deref_out_ecount_part_opt
#endif
#ifndef __deref_out_bcount_part_opt
#define __deref_out_bcount_part_opt(size,length)
#define WRAP_UNDEF__deref_out_bcount_part_opt
#endif
#ifndef __deref_out_ecount_full_opt
#define __deref_out_ecount_full_opt(size)
#define WRAP_UNDEF__deref_out_ecount_full_opt
#endif
#ifndef __deref_out_bcount_full_opt
#define __deref_out_bcount_full_opt(size)
#define WRAP_UNDEF__deref_out_bcount_full_opt
#endif
#ifndef __deref_out_z_opt
#define __deref_out_z_opt
#define WRAP_UNDEF__deref_out_z_opt
#endif
#ifndef __deref_out_ecount_z_opt
#define __deref_out_ecount_z_opt(size)
#define WRAP_UNDEF__deref_out_ecount_z_opt
#endif
#ifndef __deref_out_bcount_z_opt
#define __deref_out_bcount_z_opt(size)
#define WRAP_UNDEF__deref_out_bcount_z_opt
#endif
#ifndef __deref_out_nz_opt
#define __deref_out_nz_opt
#define WRAP_UNDEF__deref_out_nz_opt
#endif
#ifndef __deref_out_ecount_nz_opt
#define __deref_out_ecount_nz_opt(size)
#define WRAP_UNDEF__deref_out_ecount_nz_opt
#endif
#ifndef __deref_out_bcount_nz_opt
#define __deref_out_bcount_nz_opt(size)
#define WRAP_UNDEF__deref_out_bcount_nz_opt
#endif
#ifndef __deref_inout_opt
#define __deref_inout_opt
#define WRAP_UNDEF__deref_inout_opt
#endif
#ifndef __deref_inout_ecount_opt
#define __deref_inout_ecount_opt(size)
#define WRAP_UNDEF__deref_inout_ecount_opt
#endif
#ifndef __deref_inout_bcount_opt
#define __deref_inout_bcount_opt(size)
#define WRAP_UNDEF__deref_inout_bcount_opt
#endif
#ifndef __deref_inout_ecount_part_opt
#define __deref_inout_ecount_part_opt(size,length)
#define WRAP_UNDEF__deref_inout_ecount_part_opt
#endif
#ifndef __deref_inout_bcount_part_opt
#define __deref_inout_bcount_part_opt(size,length)
#define WRAP_UNDEF__deref_inout_bcount_part_opt
#endif
#ifndef __deref_inout_ecount_full_opt
#define __deref_inout_ecount_full_opt(size)
#define WRAP_UNDEF__deref_inout_ecount_full_opt
#endif
#ifndef __deref_inout_bcount_full_opt
#define __deref_inout_bcount_full_opt(size)
#define WRAP_UNDEF__deref_inout_bcount_full_opt
#endif
#ifndef __deref_inout_z_opt
#define __deref_inout_z_opt
#define WRAP_UNDEF__deref_inout_z_opt
#endif
#ifndef __deref_inout_ecount_z_opt
#define __deref_inout_ecount_z_opt(size)
#define WRAP_UNDEF__deref_inout_ecount_z_opt
#endif
#ifndef __deref_inout_bcount_z_opt
#define __deref_inout_bcount_z_opt(size)
#define WRAP_UNDEF__deref_inout_bcount_z_opt
#endif
#ifndef __deref_inout_nz_opt
#define __deref_inout_nz_opt
#define WRAP_UNDEF__deref_inout_nz_opt
#endif
#ifndef __deref_inout_ecount_nz_opt
#define __deref_inout_ecount_nz_opt(size)
#define WRAP_UNDEF__deref_inout_ecount_nz_opt
#endif
#ifndef __deref_inout_bcount_nz_opt
#define __deref_inout_bcount_nz_opt(size)
#define WRAP_UNDEF__deref_inout_bcount_nz_opt
#endif
#ifndef __deref_opt_ecount
#define __deref_opt_ecount(size)
#define WRAP_UNDEF__deref_opt_ecount
#endif
#ifndef __deref_opt_bcount
#define __deref_opt_bcount(size)
#define WRAP_UNDEF__deref_opt_bcount
#endif
#ifndef __deref_opt_out
#define __deref_opt_out
#define WRAP_UNDEF__deref_opt_out
#endif
#ifndef __deref_opt_out_z
#define __deref_opt_out_z
#define WRAP_UNDEF__deref_opt_out_z
#endif
#ifndef __deref_opt_out_ecount
#define __deref_opt_out_ecount(size)
#define WRAP_UNDEF__deref_opt_out_ecount
#endif
#ifndef __deref_opt_out_bcount
#define __deref_opt_out_bcount(size)
#define WRAP_UNDEF__deref_opt_out_bcount
#endif
#ifndef __deref_opt_out_ecount_part
#define __deref_opt_out_ecount_part(size,length)
#define WRAP_UNDEF__deref_opt_out_ecount_part
#endif
#ifndef __deref_opt_out_bcount_part
#define __deref_opt_out_bcount_part(size,length)
#define WRAP_UNDEF__deref_opt_out_bcount_part
#endif
#ifndef __deref_opt_out_ecount_full
#define __deref_opt_out_ecount_full(size)
#define WRAP_UNDEF__deref_opt_out_ecount_full
#endif
#ifndef __deref_opt_out_bcount_full
#define __deref_opt_out_bcount_full(size)
#define WRAP_UNDEF__deref_opt_out_bcount_full
#endif
#ifndef __deref_opt_inout
#define __deref_opt_inout
#define WRAP_UNDEF__deref_opt_inout
#endif
#ifndef __deref_opt_inout_ecount
#define __deref_opt_inout_ecount(size)
#define WRAP_UNDEF__deref_opt_inout_ecount
#endif
#ifndef __deref_opt_inout_bcount
#define __deref_opt_inout_bcount(size)
#define WRAP_UNDEF__deref_opt_inout_bcount
#endif
#ifndef __deref_opt_inout_ecount_part
#define __deref_opt_inout_ecount_part(size,length)
#define WRAP_UNDEF__deref_opt_inout_ecount_part
#endif
#ifndef __deref_opt_inout_bcount_part
#define __deref_opt_inout_bcount_part(size,length)
#define WRAP_UNDEF__deref_opt_inout_bcount_part
#endif
#ifndef __deref_opt_inout_ecount_full
#define __deref_opt_inout_ecount_full(size)
#define WRAP_UNDEF__deref_opt_inout_ecount_full
#endif
#ifndef __deref_opt_inout_bcount_full
#define __deref_opt_inout_bcount_full(size)
#define WRAP_UNDEF__deref_opt_inout_bcount_full
#endif
#ifndef __deref_opt_inout_z
#define __deref_opt_inout_z
#define WRAP_UNDEF__deref_opt_inout_z
#endif
#ifndef __deref_opt_inout_ecount_z
#define __deref_opt_inout_ecount_z(size)
#define WRAP_UNDEF__deref_opt_inout_ecount_z
#endif
#ifndef __deref_opt_inout_bcount_z
#define __deref_opt_inout_bcount_z(size)
#define WRAP_UNDEF__deref_opt_inout_bcount_z
#endif
#ifndef __deref_opt_inout_nz
#define __deref_opt_inout_nz
#define WRAP_UNDEF__deref_opt_inout_nz
#endif
#ifndef __deref_opt_inout_ecount_nz
#define __deref_opt_inout_ecount_nz(size)
#define WRAP_UNDEF__deref_opt_inout_ecount_nz
#endif
#ifndef __deref_opt_inout_bcount_nz
#define __deref_opt_inout_bcount_nz(size)
#define WRAP_UNDEF__deref_opt_inout_bcount_nz
#endif
#ifndef __deref_opt_ecount_opt
#define __deref_opt_ecount_opt(size)
#define WRAP_UNDEF__deref_opt_ecount_opt
#endif
#ifndef __deref_opt_bcount_opt
#define __deref_opt_bcount_opt(size)
#define WRAP_UNDEF__deref_opt_bcount_opt
#endif
#ifndef __deref_opt_out_opt
#define __deref_opt_out_opt
#define WRAP_UNDEF__deref_opt_out_opt
#endif
#ifndef __deref_opt_out_ecount_opt
#define __deref_opt_out_ecount_opt(size)
#define WRAP_UNDEF__deref_opt_out_ecount_opt
#endif
#ifndef __deref_opt_out_bcount_opt
#define __deref_opt_out_bcount_opt(size)
#define WRAP_UNDEF__deref_opt_out_bcount_opt
#endif
#ifndef __deref_opt_out_ecount_part_opt
#define __deref_opt_out_ecount_part_opt(size,length)
#define WRAP_UNDEF__deref_opt_out_ecount_part_opt
#endif
#ifndef __deref_opt_out_bcount_part_opt
#define __deref_opt_out_bcount_part_opt(size,length)
#define WRAP_UNDEF__deref_opt_out_bcount_part_opt
#endif
#ifndef __deref_opt_out_ecount_full_opt
#define __deref_opt_out_ecount_full_opt(size)
#define WRAP_UNDEF__deref_opt_out_ecount_full_opt
#endif
#ifndef __deref_opt_out_bcount_full_opt
#define __deref_opt_out_bcount_full_opt(size)
#define WRAP_UNDEF__deref_opt_out_bcount_full_opt
#endif
#ifndef __deref_opt_out_z_opt
#define __deref_opt_out_z_opt
#define WRAP_UNDEF__deref_opt_out_z_opt
#endif
#ifndef __deref_opt_out_ecount_z_opt
#define __deref_opt_out_ecount_z_opt(size)
#define WRAP_UNDEF__deref_opt_out_ecount_z_opt
#endif
#ifndef __deref_opt_out_bcount_z_opt
#define __deref_opt_out_bcount_z_opt(size)
#define WRAP_UNDEF__deref_opt_out_bcount_z_opt
#endif
#ifndef __deref_opt_out_nz_opt
#define __deref_opt_out_nz_opt
#define WRAP_UNDEF__deref_opt_out_nz_opt
#endif
#ifndef __deref_opt_out_ecount_nz_opt
#define __deref_opt_out_ecount_nz_opt(size)
#define WRAP_UNDEF__deref_opt_out_ecount_nz_opt
#endif
#ifndef __deref_opt_out_bcount_nz_opt
#define __deref_opt_out_bcount_nz_opt(size)
#define WRAP_UNDEF__deref_opt_out_bcount_nz_opt
#endif
#ifndef __deref_opt_inout_opt
#define __deref_opt_inout_opt
#define WRAP_UNDEF__deref_opt_inout_opt
#endif
#ifndef __deref_opt_inout_ecount_opt
#define __deref_opt_inout_ecount_opt(size)
#define WRAP_UNDEF__deref_opt_inout_ecount_opt
#endif
#ifndef __deref_opt_inout_bcount_opt
#define __deref_opt_inout_bcount_opt(size)
#define WRAP_UNDEF__deref_opt_inout_bcount_opt
#endif
#ifndef __deref_opt_inout_ecount_part_opt
#define __deref_opt_inout_ecount_part_opt(size,length)
#define WRAP_UNDEF__deref_opt_inout_ecount_part_opt
#endif
#ifndef __deref_opt_inout_bcount_part_opt
#define __deref_opt_inout_bcount_part_opt(size,length)
#define WRAP_UNDEF__deref_opt_inout_bcount_part_opt
#endif
#ifndef __deref_opt_inout_ecount_full_opt
#define __deref_opt_inout_ecount_full_opt(size)
#define WRAP_UNDEF__deref_opt_inout_ecount_full_opt
#endif
#ifndef __deref_opt_inout_bcount_full_opt
#define __deref_opt_inout_bcount_full_opt(size)
#define WRAP_UNDEF__deref_opt_inout_bcount_full_opt
#endif
#ifndef __deref_opt_inout_z_opt
#define __deref_opt_inout_z_opt
#define WRAP_UNDEF__deref_opt_inout_z_opt
#endif
#ifndef __deref_opt_inout_ecount_z_opt
#define __deref_opt_inout_ecount_z_opt(size)
#define WRAP_UNDEF__deref_opt_inout_ecount_z_opt
#endif
#ifndef __deref_opt_inout_bcount_z_opt
#define __deref_opt_inout_bcount_z_opt(size)
#define WRAP_UNDEF__deref_opt_inout_bcount_z_opt
#endif
#ifndef __deref_opt_inout_nz_opt
#define __deref_opt_inout_nz_opt
#define WRAP_UNDEF__deref_opt_inout_nz_opt
#endif
#ifndef __deref_opt_inout_ecount_nz_opt
#define __deref_opt_inout_ecount_nz_opt(size)
#define WRAP_UNDEF__deref_opt_inout_ecount_nz_opt
#endif
#ifndef __deref_opt_inout_bcount_nz_opt
#define __deref_opt_inout_bcount_nz_opt(size)
#define WRAP_UNDEF__deref_opt_inout_bcount_nz_opt
#endif
#ifndef __success
#define __success(epxr)
#define WRAP_UNDEF__success
#endif
#ifndef _Ret_notnull_
#define _Ret_notnull_
#define WRAP_UNDEF_Ret_notnull_
#endif
#ifndef _Post_writable_byte_size_
#define _Post_writable_byte_size_(n)
#define WRAP_UNDEF_Post_writable_byte_size_
#endif
#ifndef _Outptr_
#define _Outptr_
#define WRAP_UNDEF_Outptr_
#endif
#include "/home/claude/limit-nvpstate/nvapi/nvapi.h"
#ifdef WRAP_UNDEF__ecount
#undef __ecount
#undef WRAP_UNDEF__ecount
#endif
#ifdef WRAP_UNDEF__bcount
#undef __bcount
#undef WRAP_UNDEF__bcount
#endif
#ifdef WRAP_UNDEF__in
#undef __in
#undef WRAP_UNDEF__in
#endif
#ifdef WRAP_UNDEF__in_ecount
#undef __in_ecount
#undef WRAP_UNDEF__in_ecount
#endif
#ifdef WRAP_UNDEF__in_bcount
#undef __in_bcount
#undef WRAP_UNDEF__in_bcount
#endif
#ifdef WRAP_UNDEF__in_z
#undef __in_z
#undef WRAP_UNDEF__in_z
#endif
#ifdef WRAP_UNDEF__in_ecount_z
#undef __in_ecount_z
#undef WRAP_UNDEF__in_ecount_z
#endif
#ifdef WRAP_UNDEF__in_bcount_z
#undef __in_bcount_z
#undef WRAP_UNDEF__in_bcount_z
#endif
#ifdef WRAP_UNDEF__in_nz
#undef __in_nz
#undef WRAP_UNDEF__in_nz
#endif
#ifdef WRAP_UNDEF__in_ecount_nz
#undef __in_ecount_nz
#undef WRAP_UNDEF__in_ecount_nz
#endif
#ifdef WRAP_UNDEF__in_bcount_nz
#undef __in_bcount_nz
#undef WRAP_UNDEF__in_bcount_nz
#endif
#ifdef WRAP_UNDEF__out
#undef __out
#undef WRAP_UNDEF__out
#endif
#ifdef WRAP_UNDEF__out_ecount
#undef __out_ecount
#undef WRAP_UNDEF__out_ecount
#endif
#ifdef WRAP_UNDEF__out_bcount
#undef __out_bcount
#undef WRAP_UNDEF__out_bcount
#endif
#ifdef WRAP_UNDEF__out_ecount_part
#undef __out_ecount_part
#undef WRAP_UNDEF__out_ecount_part
#endif
#ifdef WRAP_UNDEF__out_bcount_part
#undef __out_bcount_part
#undef WRAP_UNDEF__out_bcount_part
#endif
#ifdef WRAP_UNDEF__out_ecount_full
#undef __out_ecount_full
#undef WRAP_UNDEF__out_ecount_full
#endif
#ifdef WRAP_UNDEF__out_bcount_full
#undef __out_bcount_full
#undef WRAP_UNDEF__out_bcount_full
#endif
#ifdef WRAP_UNDEF__out_z
#undef __out_z
#undef WRAP_UNDEF__out_z
#endif
#ifdef WRAP_UNDEF__out_z_opt
#undef __out_z_opt
#undef WRAP_UNDEF__out_z_opt
#endif
#ifdef WRAP_UNDEF__out_ecount_z
#undef __out_ecount_z
#undef WRAP_UNDEF__out_ecount_z
#endif
#ifdef WRAP_UNDEF__out_bcount_z
#undef __out_bcount_z
#undef WRAP_UNDEF__out_bcount_z
#endif
#ifdef WRAP_UNDEF__out_ecount_part_z
#undef __out_ecount_part_z
#undef WRAP_UNDEF__out_ecount_part_z
#endif
#ifdef WRAP_UNDEF__out_bcount_part_z
#undef __out_bcount_part_z
#undef WRAP_UNDEF__out_bcount_part_z
#endif
#ifdef WRAP_UNDEF__out_ecount_full_z
#undef __out_ecount_full_z
#undef WRAP_UNDEF__out_ecount_full_z
#endif
#ifdef WRAP_UNDEF__out_bcount_full_z
#undef __out_bcount_full_z
#undef WRAP_UNDEF__out_bcount_full_z
#endif
#ifdef WRAP_UNDEF__out_nz
#undef __out_nz
#undef WRAP_UNDEF__out_nz
#endif
#ifdef WRAP_UNDEF__out_nz_opt
#undef __out_nz_opt
#undef WRAP_UNDEF__out_nz_opt
#endif
#ifdef WRAP_UNDEF__out_ecount_nz
#undef __out_ecount_nz
#undef WRAP_UNDEF__out_ecount_nz
#endif
#ifdef WRAP_UNDEF__out_bcount_nz
#undef __out_bcount_nz
#undef WRAP_UNDEF__out_bcount_nz
#endif
#ifdef WRAP_UNDEF__inout
#undef __inout
#undef WRAP_UNDEF__inout
#endif
#ifdef WRAP_UNDEF__inout_ecount
#undef __inout_ecount
#undef WRAP_UNDEF__inout_ecount
#endif
#ifdef WRAP_UNDEF__inout_bcount
#undef __inout_bcount
#undef WRAP_UNDEF__inout_bcount
#endif
#ifdef WRAP_UNDEF__inout_ecount_part
#undef __inout_ecount_part
#undef WRAP_UNDEF__inout_ecount_part
#endif
#ifdef WRAP_UNDEF__inout_bcount_part
#undef __inout_bcount_part
#undef WRAP_UNDEF__inout_bcount_part
#endif
#ifdef WRAP_UNDEF__inout_ecount_full
#undef __inout_ecount_full
#undef WRAP_UNDEF__inout_ecount_full
#endif
#ifdef WRAP_UNDEF__inout_bcount_full
#undef __inout_bcount_full
#undef WRAP_UNDEF__inout_bcount_full
#endif
#ifdef WRAP_UNDEF__inout_z
#undef __inout_z
#undef WRAP_UNDEF__inout_z
#endif
#ifdef WRAP_UNDEF__inout_ecount_z
#undef __inout_ecount_z
#undef WRAP_UNDEF__inout_ecount_z
#endif
#ifdef WRAP_UNDEF__inout_bcount_z
#undef __inout_bcount_z
#undef WRAP_UNDEF__inout_bcount_z
#endif
#ifdef WRAP_UNDEF__inout_nz
#undef __inout_nz
#undef WRAP_UNDEF__inout_nz
#endif
#ifdef WRAP_UNDEF__inout_ecount_nz
#undef __inout_ecount_nz
#undef WRAP_UNDEF__inout_ecount_nz
#endif
#ifdef WRAP_UNDEF__inout_bcount_nz
#undef __inout_bcount_nz
#undef WRAP_UNDEF__inout_bcount_nz
#endif
#ifdef WRAP_UNDEF__ecount_opt
#undef __ecount_opt
#undef WRAP_UNDEF__ecount_opt
#endif
#ifdef WRAP_UNDEF__bcount_opt
#undef __bcount_opt
#undef WRAP_UNDEF__bcount_opt
#endif
#ifdef WRAP_UNDEF__in_opt
#undef __in_opt
#undef WRAP_UNDEF__in_opt
#endif
#ifdef WRAP_UNDEF__in_ecount_opt
#undef __in_ecount_opt
#undef WRAP_UNDEF__in_ecount_opt
#endif
#ifdef WRAP_UNDEF__in_bcount_opt
#undef __in_bcount_opt
#undef WRAP_UNDEF__in_bcount_opt
#endif
#ifdef WRAP_UNDEF__in_z_opt
#undef __in_z_opt
#undef WRAP_UNDEF__in_z_opt
#endif
#ifdef WRAP_UNDEF__in_ecount_z_opt
#undef __in_ecount_z_opt
#undef WRAP_UNDEF__in_ecount_z_opt
#endif
#ifdef WRAP_UNDEF__in_bcount_z_opt
#undef __in_bcount_z_opt
#undef WRAP_UNDEF__in_bcount_z_opt
#endif
#ifdef WRAP_UNDEF__in_nz_opt
#undef __in_nz_opt
#undef WRAP_UNDEF__in_nz_opt
#endif
#ifdef WRAP_UNDEF__in_ecount_nz_opt
#undef __in_ecount_nz_opt
#undef WRAP_UNDEF__in_ecount_nz_opt
#endif
#ifdef WRAP_UNDEF__in_bcount_nz_opt
#undef __in_bcount_nz_opt
#undef WRAP_UNDEF__in_bcount_nz_opt
#endif
#ifdef WRAP_UNDEF__out_opt
#undef __out_opt
#undef WRAP_UNDEF__out_opt
#endif
#ifdef WRAP_UNDEF__out_ecount_opt
#undef __out_ecount_opt
#undef WRAP_UNDEF__out_ecount_opt
#endif
#ifdef WRAP_UNDEF__out_bcount_opt
#undef __out_bcount_opt
#undef WRAP_UNDEF__out_bcount_opt
#endif
#ifdef WRAP_UNDEF__out_ecount_part_opt
#undef __out_ecount_part_opt
#undef WRAP_UNDEF__out_ecount_part_opt
#endif
#ifdef WRAP_UNDEF__out_bcount_part_opt
#undef __out_bcount_part_opt
#undef WRAP_UNDEF__out_bcount_part_opt
#endif
#ifdef WRAP_UNDEF__out_ecount_full_opt
#undef __out_ecount_full_opt
#undef WRAP_UNDEF__out_ecount_full_opt
#endif
#ifdef WRAP_UNDEF__out_bcount_full_opt
#undef __out_bcount_full_opt
#undef WRAP_UNDEF__out_bcount_full_opt
#endif
#ifdef WRAP_UNDEF__out_ecount_z_opt
#undef __out_ecount_z_opt
#undef WRAP_UNDEF__out_ecount_z_opt
#endif
#ifdef WRAP_UNDEF__out_bcount_z_opt
#undef __out_bcount_z_opt
#undef WRAP_UNDEF__out_bcount_z_opt
#endif
#ifdef WRAP_UNDEF__out_ecount_part_z_opt
#undef __out_ecount_part_z_opt
#undef WRAP_UNDEF__out_ecount_part_z_opt
#endif
#ifdef WRAP_UNDEF__out_bcount_part_z_opt
#undef __out_bcount_part_z_opt
#undef WRAP_UNDEF__out_bcount_part_z_opt
#endif
#ifdef WRAP_UNDEF__out_ecount_full_z_opt
#undef __out_ecount_full_z_opt
#undef WRAP_UNDEF__out_ecount_full_z_opt
#endif
#ifdef WRAP_UNDEF__out_bcount_full_z_opt
#undef __out_bcount_full_z_opt
#undef WRAP_UNDEF__out_bcount_full_z_opt
#endif
#ifdef WRAP_UNDEF__out_ecount_nz_opt
#undef __out_ecount_nz_opt
#undef WRAP_UNDEF__out_ecount_nz_opt
#endif
#ifdef WRAP_UNDEF__out_bcount_nz_opt
#undef __out_bcount_nz_opt
#undef WRAP_UNDEF__out_bcount_nz_opt
#endif
#ifdef WRAP_UNDEF__inout_opt
#undef __inout_opt
#undef WRAP_UNDEF__inout_opt
#endif
#ifdef WRAP_UNDEF__inout_ecount_opt
#undef __inout_ecount_opt
#undef WRAP_UNDEF__inout_ecount_opt
#endif
#ifdef WRAP_UNDEF__inout_bcount_opt
#undef __inout_bcount_opt
#undef WRAP_UNDEF__inout_bcount_opt
#endif
#ifdef WRAP_UNDEF__inout_ecount_part_opt
#undef __inout_ecount_part_opt
#undef WRAP_UNDEF__inout_ecount_part_opt
#endif
#ifdef WRAP_UNDEF__inout_bcount_part_opt
#undef __inout_bcount_part_opt
#undef WRAP_UNDEF__inout_bcount_part_opt
#endif
#ifdef WRAP_UNDEF__inout_ecount_full_opt
#undef __inout_ecount_full_opt
#undef WRAP_UNDEF__inout_ecount_full_opt
#endif
#ifdef WRAP_UNDEF__inout_bcount_full_opt
#undef __inout_bcount_full_opt
#undef WRAP_UNDEF__inout_bcount_full_opt
#endif
#ifdef WRAP_UNDEF__inout_z_opt
#undef __inout_z_opt
#undef WRAP_UNDEF__inout_z_opt
#endif
#ifdef WRAP_UNDEF__inout_ecount_z_opt
#undef __inout_ecount_z_opt
#undef WRAP_UNDEF__inout_ecount_z_opt
#endif
#ifdef WRAP_UNDEF__inout_ecount_z_opt
#undef __inout_ecount_z_opt
#undef WRAP_UNDEF__inout_ecount_z_opt
#endif
#ifdef WRAP_UNDEF__inout_bcount_z_opt
#undef __inout_bcount_z_opt
#undef WRAP_UNDEF__inout_bcount_z_opt
#endif
#ifdef WRAP_UNDEF__inout_nz_opt
#undef __inout_nz_opt
#undef WRAP_UNDEF__inout_nz_opt
#endif
#ifdef WRAP_UNDEF__inout_ecount_nz_opt
#undef __inout_ecount_nz_opt
#undef WRAP_UNDEF__inout_ecount_nz_opt
#endif
#ifdef WRAP_UNDEF__inout_bcount_nz_opt
#undef __inout_bcount_nz_opt
#undef WRAP_UNDEF__inout_bcount_nz_opt
#endif
#ifdef WRAP_UNDEF__deref_ecount
#undef __deref_ecount
#undef WRAP_UNDEF__deref_ecount
#endif
#ifdef WRAP_UNDEF__deref_bcount
#undef __deref_bcount
#undef WRAP_UNDEF__deref_bcount
#endif
#ifdef WRAP_UNDEF__deref_out
#undef __deref_out
#undef WRAP_UNDEF__deref_out
#endif
#ifdef WRAP_UNDEF__deref_out_ecount
#undef __deref_out_ecount
#undef WRAP_UNDEF__deref_out_ecount
#endif
#ifdef WRAP_UNDEF__deref_out_bcount
#undef __deref_out_bcount
#undef WRAP_UNDEF__deref_out_bcount
#endif
#ifdef WRAP_UNDEF__deref_out_ecount_part
#undef __deref_out_ecount_part
#undef WRAP_UNDEF__deref_out_ecount_part
#endif
#ifdef WRAP_UNDEF__deref_out_bcount_part
#undef __deref_out_bcount_part
#undef WRAP_UNDEF__deref_out_bcount_part
#endif
#ifdef WRAP_UNDEF__deref_out_ecount_full
#undef __deref_out_ecount_full
#undef WRAP_UNDEF__deref_out_ecount_full
#endif
#ifdef WRAP_UNDEF__deref_out_bcount_full
#undef __deref_out_bcount_full
#undef WRAP_UNDEF__deref_out_bcount_full
#endif
#ifdef WRAP_UNDEF__deref_out_z
#undef __deref_out_z
#undef WRAP_UNDEF__deref_out_z
#endif
#ifdef WRAP_UNDEF__deref_out_ecount_z
#undef __deref_out_ecount_z
#undef WRAP_UNDEF__deref_out_ecount_z
#endif
#ifdef WRAP_UNDEF__deref_out_bcount_z
#undef __deref_out_bcount_z
#undef WRAP_UNDEF__deref_out_bcount_z
#endif
#ifdef WRAP_UNDEF__deref_out_nz
#undef __deref_out_nz
#undef WRAP_UNDEF__deref_out_nz
#endif
#ifdef WRAP_UNDEF__deref_out_ecount_nz
#undef __deref_out_ecount_nz
#undef WRAP_UNDEF__deref_out_ecount_nz
#endif
#ifdef WRAP_UNDEF__deref_out_bcount_nz
#undef __deref_out_bcount_nz
#undef WRAP_UNDEF__deref_out_bcount_nz
#endif
#ifdef WRAP_UNDEF__deref_inout
#undef __deref_inout
#undef WRAP_UNDEF__deref_inout
#endif
#ifdef WRAP_UNDEF__deref_inout_z
#undef __deref_inout_z
#undef WRAP_UNDEF__deref_inout_z
#endif
#ifdef WRAP_UNDEF__deref_inout_ecount
#undef __deref_inout_ecount
#undef WRAP_UNDEF__deref_inout_ecount
#endif
#ifdef WRAP_UNDEF__deref_inout_bcount
#undef __deref_inout_bcount
#undef WRAP_UNDEF__deref_inout_bcount
#endif
#ifdef WRAP_UNDEF__deref_inout_ecount_part
#undef __deref_inout_ecount_part
#undef WRAP_UNDEF__deref_inout_ecount_part
#endif
#ifdef WRAP_UNDEF__deref_inout_bcount_part
#undef __deref_inout_bcount_part
#undef WRAP_UNDEF__deref_inout_bcount_part
#endif
#ifdef WRAP_UNDEF__deref_inout_ecount_full
#undef __deref_inout_ecount_full
#undef WRAP_UNDEF__deref_inout_ecount_full
#endif
#ifdef WRAP_UNDEF__deref_inout_bcount_full
#undef __deref_inout_bcount_full
#undef WRAP_UNDEF__deref_inout_bcount_full
#endif
#ifdef WRAP_UNDEF__deref_inout_z
#undef __deref_inout_z
#undef WRAP_UNDEF__deref_inout_z
#endif
#ifdef WRAP_UNDEF__deref_inout_ecount_z
#undef __deref_inout_ecount_z
#undef WRAP_UNDEF__deref_inout_ecount_z
#endif
#ifdef WRAP_UNDEF__deref_inout_bcount_z
#undef __deref_inout_bcount_z
#undef WRAP_UNDEF__deref_inout_bcount_z
#endif
#ifdef WRAP_UNDEF__deref_inout_nz
#undef __deref_inout_nz
#undef WRAP_UNDEF__deref_inout_nz
#endif
#ifdef WRAP_UNDEF__deref_inout_ecount_nz
#undef __deref_inout_ecount_nz
#undef WRAP_UNDEF__deref_inout_ecount_nz
#endif
#ifdef WRAP_UNDEF__deref_inout_bcount_nz
#undef __deref_inout_bcount_nz
#undef WRAP_UNDEF__deref_inout_bcount_nz
#endif
#ifdef WRAP_UNDEF__deref_ecount_opt
#undef __deref_ecount_opt
#undef WRAP_UNDEF__deref_ecount_opt
#endif
#ifdef WRAP_UNDEF__deref_bcount_opt
#undef __deref_bcount_opt
#undef WRAP_UNDEF__deref_bcount_opt
#endif
#ifdef WRAP_UNDEF__deref_out_opt
#undef __deref_out_opt
#undef WRAP_UNDEF__deref_out_opt
#endif
#ifdef WRAP_UNDEF__deref_out_ecount_opt
#undef __deref_out_ecount_opt
#undef WRAP_UNDEF__deref_out_ecount_opt
#endif
#ifdef WRAP_UNDEF__deref_out_bcount_opt
#undef __deref_out_bcount_opt
#undef WRAP_UNDEF__deref_out_bcount_opt
#endif
#ifdef WRAP_UNDEF__deref_out_ecount_part_opt
#undef __deref_out_ecount_part_opt
#undef WRAP_UNDEF__deref_out_ecount_part_opt
#endif
#ifdef WRAP_UNDEF__deref_out_bcount_part_opt
#undef __deref_out_bcount_part_opt
#undef WRAP_UNDEF__deref_out_bcount_part_opt
#endif
#ifdef WRAP_UNDEF__deref_out_ecount_full_opt
#undef __deref_out_ecount_full_opt
#undef WRAP_UNDEF__deref_out_ecount_full_opt
#endif
#ifdef WRAP_UNDEF__deref_out_bcount_full_opt
#undef __deref_out_bcount_full_opt
#undef WRAP_UNDEF__deref_out_bcount_full_opt
#endif
#ifdef WRAP_UNDEF__deref_out_z_opt
#undef __deref_out_z_opt
#undef WRAP_UNDEF__deref_out_z_opt
#endif
#ifdef WRAP_UNDEF__deref_out_ecount_z_opt
#undef __deref_out_ecount_z_opt
#undef WRAP_UNDEF__deref_out_ecount_z_opt
#endif
#ifdef WRAP_UNDEF__deref_out_bcount_z_opt
#undef __deref_out_bcount_z_opt
#undef WRAP_UNDEF__deref_out_bcount_z_opt
#endif
#ifdef WRAP_UNDEF__deref_out_nz_opt
#undef __deref_out_nz_opt
#undef WRAP_UNDEF__deref_out_nz_opt
#endif
#ifdef WRAP_UNDEF__deref_out_ecount_nz_opt
#undef __deref_out_ecount_nz_opt
#undef WRAP_UNDEF__deref_out_ecount_nz_opt
#endif
#ifdef WRAP_UNDEF__deref_out_bcount_nz_opt
#undef __deref_out_bcount_nz_opt
#undef WRAP_UNDEF__deref_out_bcount_nz_opt
#endif
#ifdef WRAP_UNDEF__deref_inout_opt
#undef __deref_inout_opt
#undef WRAP_UNDEF__deref_inout_opt
#endif
#ifdef WRAP_UNDEF__deref_inout_ecount_opt
#undef __deref_inout_ecount_opt
#undef WRAP_UNDEF__deref_inout_ecount_opt
#endif
#ifdef WRAP_UNDEF__deref_inout_bcount_opt
#undef __deref_inout_bcount_opt
#undef WRAP_UNDEF__deref_inout_bcount_opt
#endif
#ifdef WRAP_UNDEF__deref_inout_ecount_part_opt
#undef __deref_inout_ecount_part_opt
#undef WRAP_UNDEF__deref_inout_ecount_part_opt
#endif
#ifdef WRAP_UNDEF__deref_inout_bcount_part_opt
#undef __deref_inout_bcount_part_opt
#undef WRAP_UNDEF__deref_inout_bcount_part_opt
#endif
#ifdef WRAP_UNDEF__deref_inout_ecount_full_opt
#undef __deref_inout_ecount_full_opt
#undef WRAP_UNDEF__deref_inout_ecount_full_opt
#endif
#ifdef WRAP_UNDEF__deref_inout_bcount_full_opt
#undef __deref_inout_bcount_full_opt
#undef WRAP_UNDEF__deref_inout_bcount_full_opt
#endif
#ifdef WRAP_UNDEF__deref_inout_z_opt
#undef __deref_inout_z_opt
#undef WRAP_UNDEF__deref_inout_z_opt
#endif
#ifdef WRAP_UNDEF__deref_inout_ecount_z_opt
#undef __deref_inout_ecount_z_opt
#undef WRAP_UNDEF__deref_inout_ecount_z_opt
#endif
#ifdef WRAP_UNDEF__deref_inout_bcount_z_opt
#undef __deref_inout_bcount_z_opt
#undef WRAP_UNDEF__deref_inout_bcount_z_opt
#endif
#ifdef WRAP_UNDEF__deref_inout_nz_opt
#undef __deref_inout_nz_opt
#undef WRAP_UNDEF__deref_inout_nz_opt
#endif
#ifdef WRAP_UNDEF__deref_inout_ecount_nz_opt
#undef __deref_inout_ecount_nz_opt
#undef WRAP_UNDEF__deref_inout_ecount_nz_opt
#endif
#ifdef WRAP_UNDEF__deref_inout_bcount_nz_opt
#undef __deref_inout_bcount_nz_opt
#undef WRAP_UNDEF__deref_inout_bcount_nz_opt
#endif
#ifdef WRAP_UNDEF__deref_opt_ecount
#undef __deref_opt_ecount
#undef WRAP_UNDEF__deref_opt_ecount
#endif
#ifdef WRAP_UNDEF__deref_opt_bcount
#undef __deref_opt_bcount
#undef WRAP_UNDEF__deref_opt_bcount
#endif
#ifdef WRAP_UNDEF__deref_opt_out
#undef __deref_opt_out
#undef WRAP_UNDEF__deref_opt_out
#endif
#ifdef WRAP_UNDEF__deref_opt_out_z
#undef __deref_opt_out_z
#undef WRAP_UNDEF__deref_opt_out_z
#endif
#ifdef WRAP_UNDEF__deref_opt_out_ecount
#undef __deref_opt_out_ecount
#undef WRAP_UNDEF__deref_opt_out_ecount
#endif
#ifdef WRAP_UNDEF__deref_opt_out_bcount
#undef __deref_opt_out_bcount
#undef WRAP_UNDEF__deref_opt_out_bcount
#endif
#ifdef WRAP_UNDEF__deref_opt_out_ecount_part
#undef __deref_opt_out_ecount_part
#undef WRAP_UNDEF__deref_opt_out_ecount_part
#endif
#ifdef WRAP_UNDEF__deref_opt_out_bcount_part
#undef __deref_opt_out_bcount_part
#undef WRAP_UNDEF__deref_opt_out_bcount_part
#endif
#ifdef WRAP_UNDEF__deref_opt_out_ecount_full
#undef __deref_opt_out_ecount_full
#undef WRAP_UNDEF__deref_opt_out_ecount_full
#endif
#ifdef WRAP_UNDEF__deref_opt_out_bcount_full
#undef __deref_opt_out_bcount_full
#undef WRAP_UNDEF__deref_opt_out_bcount_full
#endif
#ifdef WRAP_UNDEF__deref_opt_inout
#undef __deref_opt_inout
#undef WRAP_UNDEF__deref_opt_inout
#endif
#ifdef WRAP_UNDEF__deref_opt_inout_ecount
#undef __deref_opt_inout_ecount
#undef WRAP_UNDEF__deref_opt_inout_ecount
#endif
#ifdef WRAP_UNDEF__deref_opt_inout_bcount
#undef __deref_opt_inout_bcount
#undef WRAP_UNDEF__deref_opt_inout_bcount
#endif
#ifdef WRAP_UNDEF__deref_opt_inout_ecount_part
#undef __deref_opt_inout_ecount_part
#undef WRAP_UNDEF__deref_opt_inout_ecount_part
#endif
#ifdef WRAP_UNDEF__deref_opt_inout_bcount_part
#undef __deref_opt_inout_bcount_part
#undef WRAP_UNDEF__deref_opt_inout_bcount_part
#endif
#ifdef WRAP_UNDEF__deref_opt_inout_ecount_full
#undef __deref_opt_inout_ecount_full
#undef WRAP_UNDEF__deref_opt_inout_ecount_full
#endif
#ifdef WRAP_UNDEF__deref_opt_inout_bcount_full
#undef __deref_opt_inout_bcount_full
#undef WRAP_UNDEF__deref_opt_inout_bcount_full
#endif
#ifdef WRAP_UNDEF__deref_opt_inout_z
#undef __deref_opt_inout_z
#undef WRAP_UNDEF__deref_opt_inout_z
#endif
#ifdef WRAP_UNDEF__deref_opt_inout_ecount_z
#undef __deref_opt_inout_ecount_z
#undef WRAP_UNDEF__deref_opt_inout_ecount_z
#endif
#ifdef WRAP_UNDEF__deref_opt_inout_bcount_z
#undef __deref_opt_inout_bcount_z
#undef WRAP_UNDEF__deref_opt_inout_bcount_z
#endif
#ifdef WRAP_UNDEF__deref_opt_inout_nz
#undef __deref_opt_inout_nz
#undef WRAP_UNDEF__deref_opt_inout_nz
#endif
#ifdef WRAP_UNDEF__deref_opt_inout_ecount_nz
#undef __deref_opt_inout_ecount_nz
#undef WRAP_UNDEF__deref_opt_inout_ecount_nz
#endif
#ifdef WRAP_UNDEF__deref_opt_inout_bcount_nz
#undef __deref_opt_inout_bcount_nz
#undef WRAP_UNDEF__deref_opt_inout_bcount_nz
#endif
#ifdef WRAP_UNDEF__deref_opt_ecount_opt
#undef __deref_opt_ecount_opt
#undef WRAP_UNDEF__deref_opt_ecount_opt
#endif
#ifdef WRAP_UNDEF__deref_opt_bcount_opt
#undef __deref_opt_bcount_opt
#undef WRAP_UNDEF__deref_opt_bcount_opt
#endif
#ifdef WRAP_UNDEF__deref_opt_out_opt
#undef __deref_opt_out_opt
#undef WRAP_UNDEF__deref_opt_out_opt
#endif
#ifdef WRAP_UNDEF__deref_opt_out_ecount_opt
#undef __deref_opt_out_ecount_opt
#undef WRAP_UNDEF__deref_opt_out_ecount_opt
#endif
#ifdef WRAP_UNDEF__deref_opt_out_bcount_opt
#undef __deref_opt_out_bcount_opt
#undef WRAP_UNDEF__deref_opt_out_bcount_opt
#endif
#ifdef WRAP_UNDEF__deref_opt_out_ecount_part_opt
#undef __deref_opt_out_ecount_part_opt
#undef WRAP_UNDEF__deref_opt_out_ecount_part_opt
#endif
#ifdef WRAP_UNDEF__deref_opt_out_bcount_part_opt
#undef __deref_opt_out_bcount_part_opt
#undef WRAP_UNDEF__deref_opt_out_bcount_part_opt
#endif
#ifdef WRAP_UNDEF__deref_opt_out_ecount_full_opt
#undef __deref_opt_out_ecount_full_opt
#undef WRAP_UNDEF__deref_opt_out_ecount_full_opt
#endif
#ifdef WRAP_UNDEF__deref_opt_out_bcount_full_opt
#undef __deref_opt_out_bcount_full_opt
#undef WRAP_UNDEF__deref_opt_out_bcount_full_opt
#endif
#ifdef WRAP_UNDEF__deref_opt_out_z_opt
#undef __deref_opt_out_z_opt
#undef WRAP_UNDEF__deref_opt_out_z_opt
#endif
#ifdef WRAP_UNDEF__deref_opt_out_ecount_z_opt
#undef __deref_opt_out_ecount_z_opt
#undef WRAP_UNDEF__deref_opt_out_ecount_z_opt
#endif
#ifdef WRAP_UNDEF__deref_opt_out_bcount_z_opt
#undef __deref_opt_out_bcount_z_opt
#undef WRAP_UNDEF__deref_opt_out_bcount_z_opt
#endif
#ifdef WRAP_UNDEF__deref_opt_out_nz_opt
#undef __deref_opt_out_nz_opt
#undef WRAP_UNDEF__deref_opt_out_nz_opt
#endif
#ifdef WRAP_UNDEF__deref_opt_out_ecount_nz_opt
#undef __deref_opt_out_ecount_nz_opt
#undef WRAP_UNDEF__deref_opt_out_ecount_nz_opt
#endif
#ifdef WRAP_UNDEF__deref_opt_out_bcount_nz_opt
#undef __deref_opt_out_bcount_nz_opt
#undef WRAP_UNDEF__deref_opt_out_bcount_nz_opt
#endif
#ifdef WRAP_UNDEF__deref_opt_inout_opt
#undef __deref_opt_inout_opt
#undef WRAP_UNDEF__deref_opt_inout_opt
#endif
#ifdef WRAP_UNDEF__deref_opt_inout_ecount_opt
#undef __deref_opt_inout_ecount_opt
#undef WRAP_UNDEF__deref_opt_inout_ecount_opt
#endif
#ifdef WRAP_UNDEF__deref_opt_inout_bcount_opt
#undef __deref_opt_inout_bcount_opt
#undef WRAP_UNDEF__deref_opt_inout_bcount_opt
#endif
#ifdef WRAP_UNDEF__deref_opt_inout_ecount_part_opt
#undef __deref_opt_inout_ecount_part_opt
#undef WRAP_UNDEF__deref_opt_inout_ecount_part_opt
#endif
#ifdef WRAP_UNDEF__deref_opt_inout_bcount_part_opt
#undef __deref_opt_inout_bcount_part_opt
#undef WRAP_UNDEF__deref_opt_inout_bcount_part_opt
#endif
#ifdef WRAP_UNDEF__deref_opt_inout_ecount_full_opt
#undef __deref_opt_inout_ecount_full_opt
#undef WRAP_UNDEF__deref_opt_inout_ecount_full_opt
#endif
#ifdef WRAP_UNDEF__deref_opt_inout_bcount_full_opt
#undef __deref_opt_inout_bcount_full_opt
#undef WRAP_UNDEF__deref_opt_inout_bcount_full_opt
#endif
#ifdef WRAP_UNDEF__deref_opt_inout_z_opt
#undef __deref_opt_inout_z_opt
#undef WRAP_UNDEF__deref_opt_inout_z_opt
#endif
#ifdef WRAP_UNDEF__deref_opt_inout_ecount_z_opt
#undef __deref_opt_inout_ecount_z_opt
#undef WRAP_UNDEF__deref_opt_inout_ecount_z_opt
#endif
#ifdef WRAP_UNDEF__deref_opt_inout_bcount_z_opt
#undef __deref_opt_inout_bcount_z_opt
#undef WRAP_UNDEF__deref_opt_inout_bcount_z_opt
#endif
#ifdef WRAP_UNDEF__deref_opt_inout_nz_opt
#undef __deref_opt_inout_nz_opt
#undef WRAP_UNDEF__deref_opt_inout_nz_opt
#endif
#ifdef WRAP_UNDEF__deref_opt_inout_ecount_nz_opt
#undef __deref_opt_inout_ecount_nz_opt
#undef WRAP_UNDEF__deref_opt_inout_ecount_nz_opt
#endif
#ifdef WRAP_UNDEF__deref_opt_inout_bcount_nz_opt
#undef __deref_opt_inout_bcount_nz_opt
#undef WRAP_UNDEF__deref_opt_inout_bcount_nz_opt
#endif
#ifdef WRAP_UNDEF__success
#undef __success
#undef WRAP_UNDEF__success
#endif
#ifdef WRAP_UNDEF_Ret_notnull_
#undef _Ret_notnull_
#undef WRAP_UNDEF_Ret_notnull_
#endif
#ifdef WRAP_UNDEF_Post_writable_byte_size_
#undef _Post_writable_byte_size_
#undef WRAP_UNDEF_Post_writable_byte_size_
#endif
#ifdef WRAP_UNDEF_Outptr_
#undef _Outptr_
#undef WRAP_UNDEF_Outptr_
#endif
