from flask import Flask, render_template, redirect
from flask_socketio import SocketIO
import paho.mqtt.client as mqtt

app = Flask(__name__)
app.config['SECRET_KEY'] = 'your-secret-key'

socketio = SocketIO(app)

# MQTT setup
mqtt_client = mqtt.Client()
mqtt_client.connect("test.mosquitto.org", 1883, 60)
mqtt_client.loop_start()

@app.route('/')
def index():
    return render_template('index.html')

@app.route('/pump/on')
def pump_on():
    mqtt_client.publish("smartcity/control", "pump:on")  # Turns pump ON and servo power OFF
    return redirect('/')

@app.route('/pump/off')
def pump_off():
    mqtt_client.publish("smartcity/control", "pump:off")  # Turns pump OFF and servo power ON + move to 180°
    return redirect('/')

@socketio.on('connect')
def handle_connect():
    print("Client connected to SocketIO")

# MQTT listener to forward moisture to browser
def on_mqtt_message(client, userdata, msg):
    socketio.emit('moisture_update', {'value': msg.payload.decode()})

mqtt_client.subscribe("smartcity/moisture")
mqtt_client.on_message = on_mqtt_message

if __name__ == '__main__':
    socketio.run(app, debug=True, port=5000)
