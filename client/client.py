import quickfix as fix

class Application(fix.Application):
    def onCreate(self, sessionID): pass
    def onLogon(self, sessionID): 
        print("Logon:", sessionID)
        self.sessionID = sessionID
        self.send_order()
        self.send_order_market()

    def onLogout(self, sessionID): print("Logout:", sessionID)
    def toAdmin(self, message, sessionID): pass
    def toApp(self, message, sessionID): pass
    def fromAdmin(self, message, sessionID): pass
    def fromApp(self, message, sessionID): 
        print("Received:", message)

    def send_order(self):
        order = fix.Message()
        header = order.getHeader()
        header.setField(fix.MsgType(fix.MsgType_NewOrderSingle))

        order.setField(fix.ClOrdID("ORDER123"))
        order.setField(fix.Symbol("AAPL"))
        order.setField(fix.Side(fix.Side_BUY))               # 1 = Buy
        order.setField(fix.TransactTime())
        order.setField(fix.OrdType(fix.OrdType_LIMIT))       # 2 = Limit
        # order.setField(fix.Price(150.25))
        order.setField(fix.OrderQty(100))

        fix.Session.sendToTarget(order, self.sessionID)
        print("Order Sent.")

    def send_order_market(self):
        order = fix.Message()
        header = order.getHeader()
        header.setField(fix.MsgType(fix.MsgType_NewOrderSingle))

        order.setField(fix.ClOrdID("ORDER456"))
        order.setField(fix.Symbol("USDJPY"))
        order.setField(fix.Side(fix.Side_BUY))               # 1 = Buy
        order.setField(fix.TransactTime())
        order.setField(fix.OrdType(fix.OrdType_MARKET))       # 2 = Limit
        # order.setField(fix.Price(150.25))
        order.setField(fix.OrderQty(50))

        fix.Session.sendToTarget(order, self.sessionID)
        print("Order Sent.")

# 启动客户端
settings = fix.SessionSettings("./fix.ini")
app = Application()
storeFactory = fix.FileStoreFactory(settings)
logFactory = fix.FileLogFactory(settings)
initiator = fix.SocketInitiator(app, storeFactory, settings, logFactory)

initiator.start()

input("Press <Enter> to quit.\n")
initiator.stop()

