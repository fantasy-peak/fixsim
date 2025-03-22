# fixsim

Overview
--------

fixsim is an project for FIX protocol server implementations. It can set the response format you want through the configuration file
It can be used for testing your client.

## Query interface parameters
```
curl http://127.0.0.1:2025/tag_list
curl http://127.0.0.1:2025/NewOrderSingle
curl http://127.0.0.1:2025/OrderCancelReplaceRequest
curl http://127.0.0.1:2025/OrderCancelRequest
curl http://127.0.0.1:2025/ExecutionReport
curl http://127.0.0.1:2025/OrderCancelReject
curl http://127.0.0.1:2025/BusinessMessageReject
```