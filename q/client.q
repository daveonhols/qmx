show .z.i;
/ system "sleep 5";
.client.location:`::8811;
.client.gwhdl:0N;
.z.pc:{show "closing .. "; .client.gwhdl:0N};

.client.chkh:{ if[null .client.gwhdl; show "reconn .. "; .client.gwhdl:hopen .client.location];};

/ small request object, big response
.client.big:.client.smallbig:{
    .client.chkh[];
    start:.z.p;
    r:.client.gwhdl(`.gateway.exec;({select from tbl2};0));
    show "small big got :: ",(-3!count r)," in dur :: ",-3!.z.p-start;
  };

/ big request object, big respose.
.client.bigbig:{
    .client.chkh[];
    start:.z.p;
    r:.client.gwhdl(`.gateway.exec;({select from tbl2};5120000?([] 1000?`7)));
    show "big / big got :: ",(-3!count r)," in dur :: ",-3!.z.p-start;
  };

/ big request object, small respose.
.client.bigsmall:{
    .client.chkh[];
    start:.z.p;
    r:.client.gwhdl(`.gateway.exec;({select from tbl2 where i < 6};5120000?([] 1000?`7)));
    show "big small got :: ",(-3!count r)," in dur :: ",-3!.z.p-start;
  };


.client.med:{
    .client.chkh[];
    start:.z.p;
    r:.client.gwhdl(`.gateway.exec;({select from tbl2 where i < 3000000};0));
    show "big got :: ",(-3!count r)," in dur :: ",-3!.z.p-start;
  };

.client.fast:{
    .client.chkh[];
    start:.z.p;
    r:.client.gwhdl(`.gateway.exec;({select from tbl2 where i = 0};0));
    show "fast got :: ",(-3!count r)," rows in dur :: ",-3!.z.p-start;
  };

.client.slow:{
    .client.chkh[];
    start:.z.p;
    r:.client.gwhdl(`.gateway.exec;({system "sleep 7"; select from tbl2 where i = 0};0));
    show "slow got :: ",(-3!count r)," rows in dur :: ",-3!.z.p-start;
  };

.client.ss:{
    start:.z.p;
    r:.client.location(`.gateway.exec;({ select from tbl2 where i < 5};0));
    show "ss got :: ",(-3!count r)," rows in dur :: ",-3!.z.p-start;
    show "WW :: " , -3!.z.W;
  };

.client.throw:{
    .client.chkh[];
    .client.gwhdl({'x};.client.fn_arg);
  };

.client.ssthrow:{
    .client.location({'x};.client.fn_arg);
  };

.client.killer:{
    .client.chkh[];
    start:.z.p;
    r:.client.gwhdl(`.gateway.exec;({$[1=first 1?10;exit 0;select from tbl2 where i < 3]};0));
    show "killer got :: ",(-3!count r)," rows in dur :: ",-3!.z.p-start;
  };

.client.fn_name:`$first ":" vs .z.x 0;
.client.fn_arg:`$last ":" vs .z.x 0; / eg throw:err
.client.fn:.Q.dd[`.client;.client.fn_name];
.z.ts:.client.fn;
system "t ",.z.x 1;

