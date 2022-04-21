## Kdb Gateway (POC) ##

A C++ multithreaded gateway to distribute queries to Kdb worker processes.  This POC is not robust and cannot be configured, the backend worker processes are hard coded and there is no recovery if they fail.  It exists currently to demonstrate the benefit of a truly multithreaded gateway solution for Kdb.

### Building ###

This is implemented using "modern" C++ and depends on asio.  You should download ASIO and make the include path available at "/path/to/asio/"

clang++ -std=c++17 asp.cpp -I /path/to/asio/include/ -lpthread -o cpp-gateway -O2

### Motivation ###

One of the classic problems when using Kdb is supporting multiple user queries in parallel.  This comes from the basic fact that Kdb is generally single threaded and one user query blocks all others.  There are various common approaches to solving this but they all generally have some problems, the main one being that the implementations are still typically single threaded, so the gateway itself can still not deal with multiple users in parallel.  If the gateway does almost no work for each user query the impact of this is limited, however the serialisation of data in and out of the gateway uses this single thread, so queries that generate large data sets still block the gateway for all users.  As seen below, this behaviour can easily be observed.

### Alternatives ###

#### mserve (https://github.com/KxSystems/kdb/blob/master/e/mserve.q) ####

Classic implementation of a purely async gateway.  Impressive for what it acheives in five lines of code but has various issues, particularly that the gateway itself is still single threaded so serialisation of large objects is blocking.  It also has no concept of queuing queries when all workers are busy so throughput is generally not optimal - in the case of N workers and N+1 queries, the N+1th query can end up being queued behind the slowest of the N pending queries.  Also, being asyncronous makes it a bit tedious for users to interact with, and one client submitting queries async in a tight loop can overload it and crash it.

#### -30! (https://github.com/daveonhols/qmx2/blob/master/30gw.q https://code.kx.com/q/kb/deferred-response/) ####

-30! deferred response is a feature added a few years ago in Kdb 3.6, it allows an mserve like gateway to be written that users can interact with syncronously rather than async.  This makes the general user interactions more straight forward and it is probably harder for one user to accidentally overload the server with queries.  There isn't a classic -30! gateway implementation that I know of for download but I have provided a proof of concept linked above to demonstrate the single threaded limitations of this and the mserve equivalent.

### The Issue ###

In this repo there is a Q script client.q which can simulate users querying a kdb database through a gateway.  It can be run in one of three modes: big, fast and slow.  The script worker.q shows a hypothetical database backend to which queries will be routed by the gateway.  Running a client in "big" mode will simply query all ten million rows from the in memory table "tbl2" - the query is fast but the result is "big" and requires some time to serialise from the server to the client. The "fast" client will query one row from this table, the query is fast and takes almost no time to serialise from the server to the client.  In this demo implementation, we have a gateway with two back end workers.  If we imagine two clients running in parallel, we should expect (with a good gateway implementation) there to always be a back end process ready to respond to user queries and the fast client running alongside the big client shouldn't see performance degredation.  Let's see, with the -30! gateway and two workers already started.

```
# run the "big" client, querying every 4000 ms: 
$rlwrap ~/q/l32/q client.q big 4000
# run the "fast" client, querying every 500 ms:
$rlwrap ~/q/l32/q client.q fast 500
```
The output for the "big" client is as expected:
```
"big got :: 10000000 in dur :: 0D00:00:03.233967000"
"big got :: 10000000 in dur :: 0D00:00:05.653426000"
"big got :: 10000000 in dur :: 0D00:00:03.372368000"
"big got :: 10000000 in dur :: 0D00:00:03.553996000"
"big got :: 10000000 in dur :: 0D00:00:03.227874000"
"big got :: 10000000 in dur :: 0D00:00:03.511253000"
"big got :: 10000000 in dur :: 0D00:00:03.217501000"
"big got :: 10000000 in dur :: 0D00:00:03.538194000"
"big got :: 10000000 in dur :: 0D00:00:03.225706000"
"big got :: 10000000 in dur :: 0D00:00:03.541350000"
```

The output for the "fast" client shows large and repeated stalling, queries go from 1 or 2 ms to over 1500ms.  With a good gateway implementation, users should not observe such behaviour.

```
"fast got :: 1 rows in dur :: 0D00:00:00.001201000"
"fast got :: 1 rows in dur :: 0D00:00:00.001226000"
"fast got :: 1 rows in dur :: 0D00:00:00.001088000"
"fast got :: 1 rows in dur :: 0D00:00:00.000947000"
"fast got :: 1 rows in dur :: 0D00:00:01.559729000"
"fast got :: 1 rows in dur :: 0D00:00:00.001169000"
"fast got :: 1 rows in dur :: 0D00:00:00.001415000"
"fast got :: 1 rows in dur :: 0D00:00:00.002100000"
"fast got :: 1 rows in dur :: 0D00:00:00.001182000"
"fast got :: 1 rows in dur :: 0D00:00:01.548886000"
"fast got :: 1 rows in dur :: 0D00:00:00.001116000"
"fast got :: 1 rows in dur :: 0D00:00:00.001105000"
"fast got :: 1 rows in dur :: 0D00:00:00.001165000"
"fast got :: 1 rows in dur :: 0D00:00:00.002832000"
"fast got :: 1 rows in dur :: 0D00:00:01.555882000"
"fast got :: 1 rows in dur :: 0D00:00:00.001203000"
```

Note that we can also run client.q in "slow" mode which does a simple sleep in the worker query, to simulate long running queries.  In this case we see consistent high performance for the "fast" client.  This points to the bottleneck being in the gateway rather than the workers.  If we use top to view cpu usage of the system when the "big" and "fast" clients are querying together, we can see the gateway cpu usage goes up to 100% (but not higher) for prolonged periods.

### The Solution ###

For now we run the gateway as a standalone executable.  As mentioned above, this is a POC so it can't be configured and isn't robust against failures in the back end.  It listens on a port just as a regular Q process would with -p xxxx and forwards queries to a free back end worker, blocking if none are free, in a manner very similar to the -30! gateway.  Again we can run the "big" and "fast" query clients, but this time, we find that the fast client does not get intermittently blocked.

```
"fast got :: 1 rows in dur :: 0D00:00:00.000395000"
"fast got :: 1 rows in dur :: 0D00:00:00.000427000"
"fast got :: 1 rows in dur :: 0D00:00:00.000687000"
"fast got :: 1 rows in dur :: 0D00:00:00.000514000"
"fast got :: 1 rows in dur :: 0D00:00:00.000679000"
"fast got :: 1 rows in dur :: 0D00:00:00.000483000"
"fast got :: 1 rows in dur :: 0D00:00:00.000575000"
"fast got :: 1 rows in dur :: 0D00:00:00.000743000"
"fast got :: 1 rows in dur :: 0D00:00:00.000469000"
"fast got :: 1 rows in dur :: 0D00:00:00.000708000"
"fast got :: 1 rows in dur :: 0D00:00:00.000603000"
"fast got :: 1 rows in dur :: 0D00:00:00.001327000"
"fast got :: 1 rows in dur :: 0D00:00:00.000798000"
"fast got :: 1 rows in dur :: 0D00:00:00.000776000"
"fast got :: 1 rows in dur :: 0D00:00:00.001831000"
```
