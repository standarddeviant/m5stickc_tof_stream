import sys       # builtin
import websocket # from `pip install websocket-client`
import msgpack   # from `pip install msgpack`
try:
    import thread
except ImportError:
    import _thread as thread
import time

tof_obj_list = []

def on_message(ws, message):
    # print(msgpack.unpackb(message))
    if type(message) in (bytes, bytearray):
        tof_obj_list.append(msgpack.unpackb(message))
        print(msgpack.unpackb(message))
        print(len(tof_obj_list))
    else:
        print("message = -->{}<--".format(message))

def on_data(ws, data, _type, _continue):
    print("### data ###")

def on_error(ws, error):
    print("ERR: " + str(error))

def on_close(ws):
    print("### closed ###")

def on_open(ws):
    print("### opened ###")

def tof_fetch():
    # url = "ws://m5stickc-streamer:42000"
    ws_url = "ws://m5c-tof:42001"
    websocket.enableTrace(True)
    ws = websocket.WebSocketApp(
        ws_url,
        on_message = on_message,
        on_error = on_error,
        on_close = on_close,
        on_open  = on_open)
    ws.run_forever()

def tof_aggregate():
    out = tof_obj_list.pop(0) # from front of list
    while len(tof_obj_list):
        nxt = tof_obj_list.pop(0) # from front of list
        for k in out.keys():
            out[k].extend(nxt[k])
    return out

def tof_save( _tof_obj ):
    import json
    from datetime import datetime
    fname = datetime.now().strftime("tof_%Y_%m_%d___%H_%M_%S.json")
    with open(fname, "w") as f:
        json.dump(_tof_obj, f)

def mkfig(w=12, h=6, nrow=1, ncol=1, dpi=100, style='seaborn', **kwargs):
    import matplotlib.pyplot as plt
    plt.style.use(style)
    return plt.subplots(
        nrow, ncol, figsize=(w, h), dpi=dpi, 
        facecolor='lightgray', edgecolor='k', **kwargs)

def tof_plot( _tof_obj ):
    import numpy as np
    import matplotlib.pyplot as plt
    fig, axes = mkfig(10, 6, 2, 1, gridspec_kw=dict(height_ratios=[3, 1]))
    tvec = np.arange(len(_tof_obj['tof'])) * np.median(_tof_obj['micros']) / 1e6
    axes[0].plot(tvec, _tof_obj['tof'])
    axes[1].plot(_tof_obj['micros'])

    fig.tight_layout()
    plt.show()  

if __name__ == "__main__":
    try:
        tof_fetch()
    except KeyboardInterrupt:
        print("stopped fetching tof data")
    # except Exception:
    #     print("catching any generic exception...")

    tof_obj = tof_aggregate()
    tof_save(tof_obj)
    tof_plot(tof_obj)
    sys.exit()



