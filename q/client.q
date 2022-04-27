show .z.i;
/ system "sleep 5";
.client.gwhdl:hopen`::8811;
.client.big:{
    start:.z.p;
    r:.client.gwhdl(`.gateway.exec;({select from tbl2};0));
    show "big got :: ",(-3!count r)," in dur :: ",-3!.z.p-start;
  };

.client.fast:{
    start:.z.p;
    r:.client.gwhdl(`.gateway.exec;({select from tbl2 where i = 0};0));
    show "fast got :: ",(-3!count r)," rows in dur :: ",-3!.z.p-start;
  };

.client.slow:{
    start:.z.p;
    r:.client.gwhdl(`.gateway.exec;({system "sleep 7"; select from tbl2 where i = 0};0));
    show "slow got :: ",(-3!count r)," rows in dur :: ",-3!.z.p-start;
  };

.client.ss:{
    start:.z.p;
    r:`::8811(`.gateway.exec;({ select from tbl2 where i < 5};0));
    show "ss got :: ",(-3!count r)," rows in dur :: ",-3!.z.p-start;
    show "WW :: " , -3!.z.W;
  };

.client.fn:.Q.dd[`.client;`$.z.x 0];
.z.ts:.client.fn;
system "t ",.z.x 1;

