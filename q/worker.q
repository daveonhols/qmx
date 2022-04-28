/ "worker" means the processes behind gateway that actually deal with user queries
/ eg rlwrap ~/q/l32/q worker.q -p 8833

.z.ps:.z.pg:{show (-3!.z.p)," :: ",-3!x; value x};
.z.pc:{show (-3!.z.p)," :: gone away :: ",-3!x};
tbl:([] a: `float$til 100); / not used now
tbl2:([] a:(10000000?8 9 10)?\:`char$(`int$"a")+til 26); / make a big table

/ just for compatibility so we can have same client code when calling
/ 1. .gateway in -30! q implementation
/ 2. pure pass through c++
.gateway.exec:value;
