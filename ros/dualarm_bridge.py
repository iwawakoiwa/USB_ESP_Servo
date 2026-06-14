import math
import rclpy
from rclpy.node import Node
from sensor_msgs.msg import JointState
import serial

# サーボID 0..7 に対応する関節名（実機の命名に合わせる）
JOINT_ORDER = [
    "l_joint1", "l_joint2", "l_joint3", "l_joint4",   # 左アーム -> 0..3
    "r_joint1", "r_joint2", "r_joint3", "r_joint4",   # 右アーム -> 4..7
]
# 校正: servo_deg = OFFSET[i] + SIGN[i] * (関節角[deg])
OFFSET = [90.0]*8          # 各関節の中立位置に対応するサーボ角
SIGN   = [1.0]*8           # 回転方向が逆なら -1.0

class DualArmBridge(Node):
    def __init__(self):
        super().__init__("dualarm_bridge")
        port = self.declare_parameter("port", "/dev/ttyUSB0").value
        baud = self.declare_parameter("baud", 115200).value
        self.ser = serial.Serial(port, baud, timeout=0.1)
        self.idx = {n: i for i, n in enumerate(JOINT_ORDER)}
        self.angles = [90.0]*8
        self.create_subscription(JointState, "joint_states", self.on_js, 10)

    def on_js(self, msg):
        for name, pos in zip(msg.name, msg.position):
            i = self.idx.get(name)
            if i is None:
                continue
            deg = OFFSET[i] + SIGN[i] * math.degrees(pos)   # rad -> deg + 校正
            self.angles[i] = max(0.0, min(180.0, deg))      # 物理リミット
        line = "S " + " ".join(f"{a:.1f}" for a in self.angles) + "\n"
        self.ser.write(line.encode())

def main():
    rclpy.init()
    node = DualArmBridge()
    try:
        rclpy.spin(node)
    finally:
        node.ser.close(); node.destroy_node(); rclpy.shutdown()

if __name__ == "__main__":
    main()