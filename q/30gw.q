/ show ".."
.gateway.workers:([] loc:`::8833`::8844; hdl:0N 0Ni; inuse:00b);
.gateway.pending:([] client:(),0Ni; id:0Ng; query:(::); state:`);

/ query:"2+3"
.gateway.exec:{[query]
    workers:select from .gateway.workers where not inuse, not null hdl;
    query_id:first -1?0Ng;
    insert[`.gateway.pending] ([] client:enlist .z.w; id:query_id; query:enlist query; state:`pending);
    / no workers available, wait until a query finishes to start
    if[0=count workers;
        -30!(::);:(::)];
    worker:first 1?workers;    
    .gateway.exec_in_worker[worker;query;query_id];
    -30!(::);
  };

.gateway.exec_in_worker:{[worker;query;query_id]
    update state:`running from `.gateway.pending where id = query_id;
    update inuse:1b from `.gateway.workers where hdl=worker[`hdl];
    (neg worker[`hdl])({[q;id] (neg .z.w)(`.gateway.reply;id;@[{(0b;value x)};q;{[id;err]show "fail in worker :: ",err,-3!id; (1b;err)}[id]])};query;query_id);
  };

/ query_id:first .d7.save;res:last .d7.save
.gateway.reply:{[query_id;res]
    complete_worker:.z.w;
    reply_to:first select from .gateway.pending where id = query_id;
    delete from `.gateway.pending where id = query_id;
    update inuse:0b from `.gateway.workers where hdl=complete_worker;
    -30!reply_to[`client], res;
    pending:select from .gateway.pending where state=`pending, i = first i;
    if[1=count pending;
        / there must be a worker available because we just gave one up on completition of this query !!
        show "in reply, sending out pending query :: ",-3!first pending[`id];
        worker:first 1?select from .gateway.workers where not inuse, not null hdl;
        .gateway.exec_in_worker[worker;first pending[`query]; first pending[`id]]];   
  };

.gateway.reconnect:{
    .gateway.reconnect_one each exec loc from .gateway.workers where null hdl;
  };

/ dest:first exec loc from .gateway.workers where null hdl;
/ conn:6i
.gateway.reconnect_one:{[dest]
    conn:@[{(1b;hopen x)};(dest;500);{[l;e]show "reconnect failed :: ", l, " :: ", e;(0b;0N)}[-3!dest]];
    if[first conn; update hdl:last conn from `.gateway.workers where loc=dest];
  };

.gateway.reconnect[];