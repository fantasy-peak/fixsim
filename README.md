# fixsim

Overview
--------

fixsim is an project for FIX protocol server implementations. It can set the response format you want through the configuration file
It can be used for testing your client.

## Quick Start :rocket:
* need install xmake, >= gcc version 13.1.0 (GCC)

#### Compile fixsim
```
curl -fsSL https://xmake.io/shget.text | bash
git clone https://github.com/fantasy-peak/fixsim.git
cd fixsim
xmake build -v -y
xmake install -o .
```

## Query interface parameters
```
curl http://127.0.0.1:2025/tag_list | jq
curl http://127.0.0.1:2025/NewOrderSingle | jq
curl http://127.0.0.1:2025/OrderCancelReplaceRequest | jq
curl http://127.0.0.1:2025/OrderCancelRequest | jq
curl http://127.0.0.1:2025/ExecutionReport | jq
curl http://127.0.0.1:2025/OrderCancelReject | jq
curl http://127.0.0.1:2025/BusinessMessageReject | jq
curl http://127.0.0.1:2025/TradingSessionStatus | jq

curl http://127.0.0.1:2025/NewOrderSingleYaml
curl http://127.0.0.1:2025/OrderCancelReplaceRequestYaml
curl http://127.0.0.1:2025/OrderCancelRequestYaml
curl http://127.0.0.1:2025/ExecutionReportYaml
curl http://127.0.0.1:2025/OrderCancelRejectYaml
curl http://127.0.0.1:2025/BusinessMessageRejectYaml
curl http://127.0.0.1:2025/TradingSessionStatusYaml
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

## Stress test
### open stress test
```
curl -X POST http://127.0.0.1:2025/stress  --data-binary "@data.csv" -H "Content-Type: text/csv"
curl -H  "auto_exit: true" -X POST http://127.0.0.1:2025/stress  --data-binary "@data.csv" -H "Content-Type: text/csv"
```
### 2. close stress test
```
curl http://127.0.0.1:2025/close/stress
```

## How to write configuration files
### 1. Query new order format
```
➜  fixsim git:(main) ✗ curl http://127.0.0.1:2025/NewOrderSingle | jq
  % Total    % Received % Xferd  Average Speed   Time    Time     Time  Current
                                 Dload  Upload   Total   Spent    Left  Speed
100  1604  100  1604    0     0  2050k      0 --:--:-- --:--:-- --:--:-- 1566k
{
  "Field": {
    "Account": {
      "required": "N",
      "tag": 1,
      "type": "STRING"
    },
    "ClOrdID": {
      "required": "Y",
      "tag": 11,
      "type": "STRING"
    },
    "OrdType": {
      "enum": [
        {
          "description": "MARKET",
          "value": "1"
        },
        {
          "description": "LIMIT",
          "value": "2"
        },
        {
          "description": "STOP",
          "value": "3"
        },
        {
          "description": "STOP_LIMIT",
          "value": "4"
        },
        {
          "description": "MARKET_ON_CLOSE",
          "value": "5"
        },
        {
          "description": "WITH_OR_WITHOUT",
          "value": "6"
        },
        {
          "description": "LIMIT_OR_BETTER",
          "value": "7"
        },
        {
          "description": "LIMIT_WITH_OR_WITHOUT",
          "value": "8"
        },
        {
          "description": "ON_BASIS",
          "value": "9"
        },
        {
          "description": "ON_CLOSE",
          "value": "A"
        },
        {
          "description": "LIMIT_ON_CLOSE",
          "value": "B"
        },
        {
          "description": "FOREX_MARKET",
          "value": "C"
        },
        {
          "description": "PREVIOUSLY_QUOTED",
          "value": "D"
        },
        {
          "description": "PREVIOUSLY_INDICATED",
          "value": "E"
        },
        {
          "description": "FOREX_LIMIT",
          "value": "F"
        },
        {
          "description": "FOREX_SWAP",
          "value": "G"
        },
        {
          "description": "FOREX_PREVIOUSLY_QUOTED",
          "value": "H"
        },
        {
          "description": "FUNARI",
          "value": "I"
        },
        {
          "description": "PEGGED",
          "value": "P"
        }
      ],
      "required": "Y",
      "tag": 40,
      "type": "CHAR"
    },
    "OrderQty": {
      "required": "N",
      "tag": 38,
      "type": "QTY"
    },
    "Side": {
      "enum": [
        {
          "description": "BUY",
          "value": "1"
        },
        {
          "description": "SELL",
          "value": "2"
        },
        {
          "description": "BUY_MINUS",
          "value": "3"
        },
        {
          "description": "SELL_PLUS",
          "value": "4"
        },
        {
          "description": "SELL_SHORT",
          "value": "5"
        },
        {
          "description": "SELL_SHORT_EXEMPT",
          "value": "6"
        },
        {
          "description": "UNDISCLOSED",
          "value": "7"
        },
        {
          "description": "CROSS",
          "value": "8"
        },
        {
          "description": "CROSS_SHORT",
          "value": "9"
        }
      ],
      "required": "Y",
      "tag": 54,
      "type": "CHAR"
    },
    "Symbol": {
      "required": "Y",
      "tag": 55,
      "type": "STRING"
    },
    "TransactTime": {
      "required": "Y",
      "tag": 60,
      "type": "UTCTIMESTAMP"
    }
  },
  "MsgType": "D",
  "Name": "NewOrderSingle"
}
```

### 2. Query ExecutionReport format
```
➜  fixsim git:(main) ✗ curl http://127.0.0.1:2025/ExecutionReport | jq
  % Total    % Received % Xferd  Average Speed   Time    Time     Time  Current
                                 Dload  Upload   Total   Spent    Left  Speed
100  2234  100  2234    0     0  1866k      0 --:--:-- --:--:-- --:--:-- 2181k
{
  "Field": {
    "ExecID": {
      "required": "Y",
      "tag": 17,
      "type": "STRING"
    },
    "ExecTransType": {
      "enum": [
        {
          "description": "NEW",
          "value": "0"
        },
        {
          "description": "CANCEL",
          "value": "1"
        },
        {
          "description": "CORRECT",
          "value": "2"
        },
        {
          "description": "STATUS",
          "value": "3"
        }
      ],
      "required": "Y",
      "tag": 20,
      "type": "CHAR"
    },
    "ExecType": {
      "enum": [
        {
          "description": "NEW",
          "value": "0"
        },
        {
          "description": "PARTIAL_FILL",
          "value": "1"
        },
        {
          "description": "FILL",
          "value": "2"
        },
        {
          "description": "DONE_FOR_DAY",
          "value": "3"
        },
        {
          "description": "CANCELED",
          "value": "4"
        },
        {
          "description": "REPLACED",
          "value": "5"
        },
        {
          "description": "PENDING_CANCEL",
          "value": "6"
        },
        {
          "description": "STOPPED",
          "value": "7"
        },
        {
          "description": "REJECTED",
          "value": "8"
        },
        {
          "description": "SUSPENDED",
          "value": "9"
        },
        {
          "description": "PENDING_NEW",
          "value": "A"
        },
        {
          "description": "CALCULATED",
          "value": "B"
        },
        {
          "description": "EXPIRED",
          "value": "C"
        },
        {
          "description": "RESTATED",
          "value": "D"
        },
        {
          "description": "PENDING_REPLACE",
          "value": "E"
        }
      ],
      "required": "Y",
      "tag": 150,
      "type": "CHAR"
    },
    "OrdStatus": {
      "enum": [
        {
          "description": "NEW",
          "value": "0"
        },
        {
          "description": "PARTIALLY_FILLED",
          "value": "1"
        },
        {
          "description": "FILLED",
          "value": "2"
        },
        {
          "description": "DONE_FOR_DAY",
          "value": "3"
        },
        {
          "description": "CANCELED",
          "value": "4"
        },
        {
          "description": "REPLACED",
          "value": "5"
        },
        {
          "description": "PENDING_CANCEL",
          "value": "6"
        },
        {
          "description": "STOPPED",
          "value": "7"
        },
        {
          "description": "REJECTED",
          "value": "8"
        },
        {
          "description": "SUSPENDED",
          "value": "9"
        },
        {
          "description": "PENDING_NEW",
          "value": "A"
        },
        {
          "description": "CALCULATED",
          "value": "B"
        },
        {
          "description": "EXPIRED",
          "value": "C"
        },
        {
          "description": "ACCEPTED_FOR_BIDDING",
          "value": "D"
        },
        {
          "description": "PENDING_REPLACE",
          "value": "E"
        }
      ],
      "required": "Y",
      "tag": 39,
      "type": "CHAR"
    },
    "OrderID": {
      "required": "Y",
      "tag": 37,
      "type": "STRING"
    },
    "Side": {
      "enum": [
        {
          "description": "BUY",
          "value": "1"
        },
        {
          "description": "SELL",
          "value": "2"
        },
        {
          "description": "BUY_MINUS",
          "value": "3"
        },
        {
          "description": "SELL_PLUS",
          "value": "4"
        },
        {
          "description": "SELL_SHORT",
          "value": "5"
        },
        {
          "description": "SELL_SHORT_EXEMPT",
          "value": "6"
        },
        {
          "description": "UNDISCLOSED",
          "value": "7"
        },
        {
          "description": "CROSS",
          "value": "8"
        },
        {
          "description": "CROSS_SHORT",
          "value": "9"
        }
      ],
      "required": "Y",
      "tag": 54,
      "type": "CHAR"
    },
    "Symbol": {
      "required": "Y",
      "tag": 55,
      "type": "STRING"
    },
    "TransactTime": {
      "required": "N",
      "tag": 60,
      "type": "UTCTIMESTAMP"
    }
  },
  "MsgType": "8",
  "Name": "ExecutionReport"
}
```

### 3. Write configuration file
[cfg_1.yaml](https://github.com/fantasy-peak/fixsim/blob/main/cfg/cfg_1.yaml)
[cfg_2.yaml](https://github.com/fantasy-peak/fixsim/blob/main/cfg/cfg_2.yaml)
