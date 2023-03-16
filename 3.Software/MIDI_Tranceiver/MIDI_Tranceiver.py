import mido
import serial

input_ports = mido.get_input_names()
print("Input ports:", input_ports)
input_port_index = 0
in_port = mido.open_input(input_ports[input_port_index])

serial_port = 'COM3'
ser = serial.Serial(
    port=serial_port,
    baudrate=115200,
    bytesize=serial.EIGHTBITS,
    parity=serial.PARITY_NONE,
    stopbits=serial.STOPBITS_ONE,
    timeout=None,
    xonxoff=False,
    rtscts=False,
    dsrdtr=False
)

try:
    print("读取发送信息喵！")
    while True:
        for msg in in_port.iter_pending():
            print("收到的MIDI信息喵:", msg)
            if msg.type == 'note_on':
                data = [1, msg.note, msg.velocity]
                print(data)
                ser.write(bytes(data))
            if msg.type == 'note_off':
                data = [0, msg.note, msg.velocity]
                print(data)
                ser.write(bytes(data))
except KeyboardInterrupt:
    print("有错误退出了喵...")

in_port.close()
