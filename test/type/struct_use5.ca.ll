; ModuleID = 'struct_use5.ca'
source_filename = "struct_use5.ca"

%A5 = type { %A4, %A4*, %A4**, %A4***, %A4**** }
%A4 = type { %AA*, %A1**, %A3*, %A2** }
%AA = type {}
%A1 = type {}
%A3 = type { %AA, %A1, %A2 }
%A2 = type { i32, i64, i32, float, double, i1, i8, i8 }
%A5.0 = type { %A4, %A4*, %A4**, %A4***, %A4****, i1 }

@v5 = internal global %A5 zeroinitializer, align 4
@0 = private unnamed_addr constant [3 x i8] c"%c\00", align 1

declare i32 @printf(i8*, ...)

define void @main() {
entry:
  %v5 = alloca %A5.0, align 8
  %0 = bitcast %A5.0* %v5 to i8*
  call void @llvm.memset.p0i8.i64(i8* align 8 %0, i8 0, i64 72, i1 false)
  %n = call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([3 x i8], [3 x i8]* @0, i32 0, i32 0), i8 103)
  %n1 = call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([3 x i8], [3 x i8]* @0, i32 0, i32 0), i8 111)
  %n2 = call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([3 x i8], [3 x i8]* @0, i32 0, i32 0), i8 111)
  %n3 = call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([3 x i8], [3 x i8]* @0, i32 0, i32 0), i8 100)
  %n4 = call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([3 x i8], [3 x i8]* @0, i32 0, i32 0), i8 10)
  br label %ret

ret:                                              ; preds = %entry
  ret void
}

; Function Attrs: argmemonly nofree nosync nounwind willreturn writeonly
declare void @llvm.memset.p0i8.i64(i8* nocapture writeonly, i8, i64, i1 immarg) #0

attributes #0 = { argmemonly nofree nosync nounwind willreturn writeonly }
