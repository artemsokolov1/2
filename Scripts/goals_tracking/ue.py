import sys, time
sys.path.insert(0, r"F:/UnrealEngine/UE_5.8/Engine/Plugins/Experimental/PythonScriptPlugin/Content/Python")
import remote_execution as re_
code = open(sys.argv[1], encoding="utf-8").read() if len(sys.argv) > 1 else sys.stdin.read()
cfg = re_.RemoteExecutionConfig()
cfg.multicast_bind_address = "0.0.0.0"
r = re_.RemoteExecution(cfg)
r.start()
node = None
for _ in range(50):
    if r.remote_nodes:
        node = r.remote_nodes[0]["node_id"]; break
    time.sleep(0.1)
if not node:
    print("NO NODE"); r.stop(); sys.exit(1)
r.open_command_connection(node)
res = r.run_command(code, exec_mode=re_.MODE_EXEC_FILE)
for o in res.get("output", []):
    print(o["output"].rstrip())
if not res.get("success"):
    print("ERROR:", res.get("result"))
r.stop()
