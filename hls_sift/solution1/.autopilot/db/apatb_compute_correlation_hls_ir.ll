; ModuleID = 'C:/workspace/AURORA/hls_sift/solution1/.autopilot/db/a.g.ld.5.gdce.bc'
source_filename = "llvm-link"
target datalayout = "e-m:e-i64:64-i128:128-i256:256-i512:512-i1024:1024-i2048:2048-i4096:4096-n8:16:32:64-S128-v16:16-v24:32-v32:32-v48:64-v96:128-v192:256-v256:256-v512:512-v1024:1024"
target triple = "fpga64-xilinx-none"

%struct.CorrResult = type { i32, i16 }

; Function Attrs: noinline
define void @apatb_compute_correlation_hls_ir(i64* noalias nocapture nonnull readonly "fpga.decayed.dim.hint"="4096" "maxi" %alice_tags, i8* noalias nocapture nonnull readonly "fpga.decayed.dim.hint"="4096" "maxi" %alice_meas, i64* noalias nocapture nonnull readonly "fpga.decayed.dim.hint"="4096" "maxi" %bob_tags, i8* noalias nocapture nonnull readonly "fpga.decayed.dim.hint"="4096" "maxi" %bob_meas, i32 %size_a, i32 %size_b, i64 %window, i16 signext %fixed_lag, i8 zeroext %mode, i8* noalias nocapture nonnull "maxi" %sifted_key_out, i32* noalias nocapture nonnull "maxi" %sifted_count, %struct.CorrResult* noalias nocapture nonnull dereferenceable(8) %result) local_unnamed_addr #0 {
entry:
  %malloccall = tail call i8* @malloc(i64 32768)
  %alice_tags_copy = bitcast i8* %malloccall to [4096 x i64]*
  %malloccall1 = tail call i8* @malloc(i64 4096)
  %alice_meas_copy = bitcast i8* %malloccall1 to [4096 x i8]*
  %malloccall2 = tail call i8* @malloc(i64 32768)
  %bob_tags_copy = bitcast i8* %malloccall2 to [4096 x i64]*
  %malloccall3 = tail call i8* @malloc(i64 4096)
  %bob_meas_copy = bitcast i8* %malloccall3 to [4096 x i8]*
  %malloccall4 = tail call i8* @malloc(i64 4096)
  %sifted_key_out_copy = bitcast i8* %malloccall4 to [4096 x i8]*
  %sifted_count_copy = alloca i32, align 512
  %result_copy = alloca i48, align 512
  %0 = bitcast i64* %alice_tags to [4096 x i64]*
  %1 = bitcast i8* %alice_meas to [4096 x i8]*
  %2 = bitcast i64* %bob_tags to [4096 x i64]*
  %3 = bitcast i8* %bob_meas to [4096 x i8]*
  %4 = bitcast i8* %sifted_key_out to [4096 x i8]*
  call fastcc void @copy_in([4096 x i64]* nonnull %0, [4096 x i64]* %alice_tags_copy, [4096 x i8]* nonnull %1, [4096 x i8]* %alice_meas_copy, [4096 x i64]* nonnull %2, [4096 x i64]* %bob_tags_copy, [4096 x i8]* nonnull %3, [4096 x i8]* %bob_meas_copy, [4096 x i8]* nonnull %4, [4096 x i8]* %sifted_key_out_copy, i32* nonnull %sifted_count, i32* nonnull align 512 %sifted_count_copy, %struct.CorrResult* nonnull %result, i48* nonnull align 512 %result_copy)
  call void @apatb_compute_correlation_hls_hw([4096 x i64]* %alice_tags_copy, [4096 x i8]* %alice_meas_copy, [4096 x i64]* %bob_tags_copy, [4096 x i8]* %bob_meas_copy, i32 %size_a, i32 %size_b, i64 %window, i16 %fixed_lag, i8 %mode, [4096 x i8]* %sifted_key_out_copy, i32* %sifted_count_copy, i48* %result_copy)
  call void @copy_back([4096 x i64]* %0, [4096 x i64]* %alice_tags_copy, [4096 x i8]* %1, [4096 x i8]* %alice_meas_copy, [4096 x i64]* %2, [4096 x i64]* %bob_tags_copy, [4096 x i8]* %3, [4096 x i8]* %bob_meas_copy, [4096 x i8]* %4, [4096 x i8]* %sifted_key_out_copy, i32* %sifted_count, i32* %sifted_count_copy, %struct.CorrResult* %result, i48* %result_copy)
  tail call void @free(i8* %malloccall)
  tail call void @free(i8* %malloccall1)
  tail call void @free(i8* %malloccall2)
  tail call void @free(i8* %malloccall3)
  tail call void @free(i8* %malloccall4)
  ret void
}

declare noalias i8* @malloc(i64) local_unnamed_addr

; Function Attrs: argmemonly noinline norecurse willreturn
define internal fastcc void @copy_in([4096 x i64]* noalias readonly, [4096 x i64]* noalias, [4096 x i8]* noalias readonly, [4096 x i8]* noalias, [4096 x i64]* noalias readonly, [4096 x i64]* noalias, [4096 x i8]* noalias readonly, [4096 x i8]* noalias, [4096 x i8]* noalias readonly, [4096 x i8]* noalias, i32* noalias readonly, i32* noalias align 512, %struct.CorrResult* noalias readonly, i48* noalias align 512) unnamed_addr #1 {
entry:
  call fastcc void @onebyonecpy_hls.p0a4096i64([4096 x i64]* %1, [4096 x i64]* %0)
  call fastcc void @onebyonecpy_hls.p0a4096i8([4096 x i8]* %3, [4096 x i8]* %2)
  call fastcc void @onebyonecpy_hls.p0a4096i64([4096 x i64]* %5, [4096 x i64]* %4)
  call fastcc void @onebyonecpy_hls.p0a4096i8([4096 x i8]* %7, [4096 x i8]* %6)
  call fastcc void @onebyonecpy_hls.p0a4096i8([4096 x i8]* %9, [4096 x i8]* %8)
  call fastcc void @onebyonecpy_hls.p0i32(i32* align 512 %11, i32* %10)
  call fastcc void @onebyonecpy_hls.p0struct.CorrResult(i48* align 512 %13, %struct.CorrResult* %12)
  ret void
}

; Function Attrs: argmemonly noinline norecurse willreturn
define internal fastcc void @onebyonecpy_hls.p0a4096i64([4096 x i64]* noalias %dst, [4096 x i64]* noalias readonly %src) unnamed_addr #2 {
entry:
  %0 = icmp eq [4096 x i64]* %dst, null
  %1 = icmp eq [4096 x i64]* %src, null
  %2 = or i1 %0, %1
  br i1 %2, label %ret, label %copy

copy:                                             ; preds = %entry
  call void @arraycpy_hls.p0a4096i64([4096 x i64]* nonnull %dst, [4096 x i64]* nonnull %src, i64 4096)
  br label %ret

ret:                                              ; preds = %copy, %entry
  ret void
}

; Function Attrs: argmemonly noinline norecurse willreturn
define void @arraycpy_hls.p0a4096i64([4096 x i64]* %dst, [4096 x i64]* readonly %src, i64 %num) local_unnamed_addr #3 {
entry:
  %0 = icmp eq [4096 x i64]* %src, null
  %1 = icmp eq [4096 x i64]* %dst, null
  %2 = or i1 %1, %0
  br i1 %2, label %ret, label %copy

copy:                                             ; preds = %entry
  %for.loop.cond1 = icmp sgt i64 %num, 0
  br i1 %for.loop.cond1, label %for.loop.lr.ph, label %copy.split

for.loop.lr.ph:                                   ; preds = %copy
  br label %for.loop

for.loop:                                         ; preds = %for.loop, %for.loop.lr.ph
  %for.loop.idx2 = phi i64 [ 0, %for.loop.lr.ph ], [ %for.loop.idx.next, %for.loop ]
  %dst.addr = getelementptr [4096 x i64], [4096 x i64]* %dst, i64 0, i64 %for.loop.idx2
  %src.addr = getelementptr [4096 x i64], [4096 x i64]* %src, i64 0, i64 %for.loop.idx2
  %3 = load i64, i64* %src.addr, align 8
  store i64 %3, i64* %dst.addr, align 8
  %for.loop.idx.next = add nuw nsw i64 %for.loop.idx2, 1
  %exitcond = icmp ne i64 %for.loop.idx.next, %num
  br i1 %exitcond, label %for.loop, label %copy.split

copy.split:                                       ; preds = %for.loop, %copy
  br label %ret

ret:                                              ; preds = %copy.split, %entry
  ret void
}

; Function Attrs: argmemonly noinline norecurse willreturn
define internal fastcc void @onebyonecpy_hls.p0a4096i8([4096 x i8]* noalias %dst, [4096 x i8]* noalias readonly %src) unnamed_addr #2 {
entry:
  %0 = icmp eq [4096 x i8]* %dst, null
  %1 = icmp eq [4096 x i8]* %src, null
  %2 = or i1 %0, %1
  br i1 %2, label %ret, label %copy

copy:                                             ; preds = %entry
  call void @arraycpy_hls.p0a4096i8([4096 x i8]* nonnull %dst, [4096 x i8]* nonnull %src, i64 4096)
  br label %ret

ret:                                              ; preds = %copy, %entry
  ret void
}

; Function Attrs: argmemonly noinline norecurse willreturn
define void @arraycpy_hls.p0a4096i8([4096 x i8]* %dst, [4096 x i8]* readonly %src, i64 %num) local_unnamed_addr #3 {
entry:
  %0 = icmp eq [4096 x i8]* %src, null
  %1 = icmp eq [4096 x i8]* %dst, null
  %2 = or i1 %1, %0
  br i1 %2, label %ret, label %copy

copy:                                             ; preds = %entry
  %for.loop.cond1 = icmp sgt i64 %num, 0
  br i1 %for.loop.cond1, label %for.loop.lr.ph, label %copy.split

for.loop.lr.ph:                                   ; preds = %copy
  br label %for.loop

for.loop:                                         ; preds = %for.loop, %for.loop.lr.ph
  %for.loop.idx2 = phi i64 [ 0, %for.loop.lr.ph ], [ %for.loop.idx.next, %for.loop ]
  %dst.addr = getelementptr [4096 x i8], [4096 x i8]* %dst, i64 0, i64 %for.loop.idx2
  %src.addr = getelementptr [4096 x i8], [4096 x i8]* %src, i64 0, i64 %for.loop.idx2
  %3 = load i8, i8* %src.addr, align 1
  store i8 %3, i8* %dst.addr, align 1
  %for.loop.idx.next = add nuw nsw i64 %for.loop.idx2, 1
  %exitcond = icmp ne i64 %for.loop.idx.next, %num
  br i1 %exitcond, label %for.loop, label %copy.split

copy.split:                                       ; preds = %for.loop, %copy
  br label %ret

ret:                                              ; preds = %copy.split, %entry
  ret void
}

; Function Attrs: argmemonly noinline norecurse willreturn
define internal fastcc void @onebyonecpy_hls.p0i32(i32* noalias align 512 %dst, i32* noalias readonly %src) unnamed_addr #2 {
entry:
  %0 = icmp eq i32* %dst, null
  %1 = icmp eq i32* %src, null
  %2 = or i1 %0, %1
  br i1 %2, label %ret, label %copy

copy:                                             ; preds = %entry
  %3 = load i32, i32* %src, align 4
  store i32 %3, i32* %dst, align 512
  br label %ret

ret:                                              ; preds = %copy, %entry
  ret void
}

; Function Attrs: argmemonly noinline norecurse willreturn
define internal fastcc void @onebyonecpy_hls.p0struct.CorrResult(i48* noalias align 512 %dst, %struct.CorrResult* noalias readonly %src) unnamed_addr #2 {
entry:
  %0 = icmp eq i48* %dst, null
  %1 = icmp eq %struct.CorrResult* %src, null
  %2 = or i1 %0, %1
  br i1 %2, label %ret, label %copy

copy:                                             ; preds = %entry
  %src.0 = getelementptr %struct.CorrResult, %struct.CorrResult* %src, i64 0, i32 0
  %3 = load i32, i32* %src.0, align 4
  %4 = zext i32 %3 to i48
  %src.1 = getelementptr %struct.CorrResult, %struct.CorrResult* %src, i64 0, i32 1
  %5 = load i16, i16* %src.1, align 2
  %6 = zext i16 %5 to i48
  %7 = shl i48 %6, 32
  %.partset = or i48 %7, %4
  store i48 %.partset, i48* %dst, align 512
  br label %ret

ret:                                              ; preds = %copy, %entry
  ret void
}

; Function Attrs: argmemonly noinline norecurse willreturn
define internal fastcc void @copy_out([4096 x i64]* noalias, [4096 x i64]* noalias readonly, [4096 x i8]* noalias, [4096 x i8]* noalias readonly, [4096 x i64]* noalias, [4096 x i64]* noalias readonly, [4096 x i8]* noalias, [4096 x i8]* noalias readonly, [4096 x i8]* noalias, [4096 x i8]* noalias readonly, i32* noalias, i32* noalias readonly align 512, %struct.CorrResult* noalias, i48* noalias readonly align 512) unnamed_addr #4 {
entry:
  call fastcc void @onebyonecpy_hls.p0a4096i64([4096 x i64]* %0, [4096 x i64]* %1)
  call fastcc void @onebyonecpy_hls.p0a4096i8([4096 x i8]* %2, [4096 x i8]* %3)
  call fastcc void @onebyonecpy_hls.p0a4096i64([4096 x i64]* %4, [4096 x i64]* %5)
  call fastcc void @onebyonecpy_hls.p0a4096i8([4096 x i8]* %6, [4096 x i8]* %7)
  call fastcc void @onebyonecpy_hls.p0a4096i8([4096 x i8]* %8, [4096 x i8]* %9)
  call fastcc void @onebyonecpy_hls.p0i32(i32* %10, i32* align 512 %11)
  call fastcc void @onebyonecpy_hls.p0struct.CorrResult.6(%struct.CorrResult* %12, i48* align 512 %13)
  ret void
}

declare void @free(i8*) local_unnamed_addr

; Function Attrs: argmemonly noinline norecurse willreturn
define internal fastcc void @onebyonecpy_hls.p0struct.CorrResult.6(%struct.CorrResult* noalias %dst, i48* noalias readonly align 512 %src) unnamed_addr #2 {
entry:
  %0 = icmp eq %struct.CorrResult* %dst, null
  %1 = icmp eq i48* %src, null
  %2 = or i1 %0, %1
  br i1 %2, label %ret, label %copy

copy:                                             ; preds = %entry
  %dst.0 = getelementptr %struct.CorrResult, %struct.CorrResult* %dst, i64 0, i32 0
  %3 = load i48, i48* %src, align 512
  %.partselect1 = trunc i48 %3 to i32
  store i32 %.partselect1, i32* %dst.0, align 512
  %dst.1 = getelementptr %struct.CorrResult, %struct.CorrResult* %dst, i64 0, i32 1
  %4 = lshr i48 %3, 32
  %.partselect = trunc i48 %4 to i16
  store i16 %.partselect, i16* %dst.1, align 4
  br label %ret

ret:                                              ; preds = %copy, %entry
  ret void
}

declare void @apatb_compute_correlation_hls_hw([4096 x i64]*, [4096 x i8]*, [4096 x i64]*, [4096 x i8]*, i32, i32, i64, i16, i8, [4096 x i8]*, i32*, i48*)

; Function Attrs: argmemonly noinline norecurse willreturn
define internal fastcc void @copy_back([4096 x i64]* noalias, [4096 x i64]* noalias readonly, [4096 x i8]* noalias, [4096 x i8]* noalias readonly, [4096 x i64]* noalias, [4096 x i64]* noalias readonly, [4096 x i8]* noalias, [4096 x i8]* noalias readonly, [4096 x i8]* noalias, [4096 x i8]* noalias readonly, i32* noalias, i32* noalias readonly align 512, %struct.CorrResult* noalias, i48* noalias readonly align 512) unnamed_addr #4 {
entry:
  call fastcc void @onebyonecpy_hls.p0a4096i8([4096 x i8]* %8, [4096 x i8]* %9)
  call fastcc void @onebyonecpy_hls.p0i32(i32* %10, i32* align 512 %11)
  call fastcc void @onebyonecpy_hls.p0struct.CorrResult.6(%struct.CorrResult* %12, i48* align 512 %13)
  ret void
}

define void @compute_correlation_hls_hw_stub_wrapper([4096 x i64]*, [4096 x i8]*, [4096 x i64]*, [4096 x i8]*, i32, i32, i64, i16, i8, [4096 x i8]*, i32*, i48*) #5 {
entry:
  %12 = alloca %struct.CorrResult
  call void @copy_out([4096 x i64]* null, [4096 x i64]* %0, [4096 x i8]* null, [4096 x i8]* %1, [4096 x i64]* null, [4096 x i64]* %2, [4096 x i8]* null, [4096 x i8]* %3, [4096 x i8]* null, [4096 x i8]* %9, i32* null, i32* %10, %struct.CorrResult* %12, i48* %11)
  %13 = bitcast [4096 x i64]* %0 to i64*
  %14 = bitcast [4096 x i8]* %1 to i8*
  %15 = bitcast [4096 x i64]* %2 to i64*
  %16 = bitcast [4096 x i8]* %3 to i8*
  %17 = bitcast [4096 x i8]* %9 to i8*
  call void @compute_correlation_hls_hw_stub(i64* %13, i8* %14, i64* %15, i8* %16, i32 %4, i32 %5, i64 %6, i16 %7, i8 %8, i8* %17, i32* %10, %struct.CorrResult* %12)
  call void @copy_in([4096 x i64]* null, [4096 x i64]* %0, [4096 x i8]* null, [4096 x i8]* %1, [4096 x i64]* null, [4096 x i64]* %2, [4096 x i8]* null, [4096 x i8]* %3, [4096 x i8]* null, [4096 x i8]* %9, i32* null, i32* %10, %struct.CorrResult* %12, i48* %11)
  ret void
}

declare void @compute_correlation_hls_hw_stub(i64*, i8*, i64*, i8*, i32, i32, i64, i16, i8, i8*, i32*, %struct.CorrResult*)

attributes #0 = { noinline "fpga.wrapper.func"="wrapper" }
attributes #1 = { argmemonly noinline norecurse willreturn "fpga.wrapper.func"="copyin" }
attributes #2 = { argmemonly noinline norecurse willreturn "fpga.wrapper.func"="onebyonecpy_hls" }
attributes #3 = { argmemonly noinline norecurse willreturn "fpga.wrapper.func"="arraycpy_hls" }
attributes #4 = { argmemonly noinline norecurse willreturn "fpga.wrapper.func"="copyout" }
attributes #5 = { "fpga.wrapper.func"="stub" }

!llvm.dbg.cu = !{}
!llvm.ident = !{!0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0, !0}
!llvm.module.flags = !{!1, !2, !3}
!blackbox_cfg = !{!4}

!0 = !{!"clang version 7.0.0 "}
!1 = !{i32 2, !"Dwarf Version", i32 4}
!2 = !{i32 2, !"Debug Info Version", i32 3}
!3 = !{i32 1, !"wchar_size", i32 4}
!4 = !{}
