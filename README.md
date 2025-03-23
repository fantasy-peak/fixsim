# fixsim

Overview
--------

fixsim is an project for FIX protocol server implementations. It can set the response format you want through the configuration file
It can be used for testing your client.

## Query interface parameters
```
curl http://127.0.0.1:2025/tag_list | jq
curl http://127.0.0.1:2025/NewOrderSingle | jq
curl http://127.0.0.1:2025/OrderCancelReplaceRequest | jq
curl http://127.0.0.1:2025/OrderCancelRequest | jq
curl http://127.0.0.1:2025/ExecutionReport | jq
curl http://127.0.0.1:2025/OrderCancelReject | jq
curl http://127.0.0.1:2025/BusinessMessageReject | jq
```

## Control message sending
### 1. Pause send message
```
curl -X POST http://127.0.0.1:2025/pause -d '{"flag": true }' -v
```
### 2. Lift the pause
```
curl -X POST http://127.0.0.1:2025/pause -d '{"flag": false }' -v
```

## How to write configuration files
### 1. Query new order format
```
(env) ➜  client git:(main) ✗ curl http://127.0.0.1:2025/NewOrderSingle | jq
  % Total    % Received % Xferd  Average Speed   Time    Time     Time  Current
                                 Dload  Upload   Total   Spent    Left  Speed
100   274  100   274    0     0   268k      0 --:--:-- --:--:-- --:--:--  267k
{
  "Field": {
    "Account": "tag(1) Required(N)",
    "ClOrdID": "tag(11) Required(Y)",
    "OrdType": "tag(40) Required(Y)",
    "OrderQty": "tag(38) Required(N)",
    "Side": "tag(54) Required(Y)",
    "Symbol": "tag(55) Required(Y)",
    "TransactTime": "tag(60) Required(Y)"
  },
  "MsgType": "D",
  "Name": "NewOrderSingle"
}
```

### 2. Query ExecutionReport format
```
(env) ➜  client git:(main) ✗ curl http://127.0.0.1:2025/ExecutionReport | jq
  % Total    % Received % Xferd  Average Speed   Time    Time     Time  Current
                                 Dload  Upload   Total   Spent    Left  Speed
100   316  100   316    0     0   361k      0 --:--:-- --:--:-- --:--:--  308k
{
  "Field": {
    "ExecID": "tag(17) Required(Y)",
    "ExecTransType": "tag(20) Required(Y)",
    "ExecType": "tag(150) Required(Y)",
    "OrdStatus": "tag(39) Required(Y)",
    "OrderID": "tag(37) Required(Y)",
    "Side": "tag(54) Required(Y)",
    "Symbol": "tag(55) Required(Y)",
    "TransactTime": "tag(60) Required(N)"
  },
  "MsgType": "8",
  "Name": "ExecutionReport"
}
```

### 3. Write configuration file
[cfg.yaml](https://github.com/fantasy-peak/fixsim/blob/main/cfg/cfg.yaml)

