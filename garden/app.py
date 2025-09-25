from flask import Flask, render_template, redirect, url_for
from flask_socketio import SocketIO
import paho.mqtt.client as mqtt

app = Flask(__name__)
app.config['SECRET_KEY'] = 'your-secret-key'
socketio = SocketIO(app)

# MQTT setup
mqtt_client = mqtt.Client()
mqtt_client.connect("test.mosquitto.org", 1883, 60)
mqtt_client.loop_start()

# Store moisture status
moisture_status = "Loading..."  # Default message until the first update

# MQTT callback to handle received messages
def on_message(client, userdata, msg):
    global moisture_status
    if msg.topic == "smartcity/moisture":
        moisture_status = msg.payload.decode("utf-8")
        print(f"Updated Moisture Status: {moisture_status}")
        socketio.emit('moisture_update', {'status': moisture_status})

mqtt_client.subscribe("smartcity/moisture")
mqtt_client.on_message = on_message

@app.route('/')
def index():
    return render_template('index.html', moisture_status=moisture_status)

@app.route('/pump/on')
def pump_on():
    print("Flask: Publishing pump:on")
    mqtt_client.publish("smartcity/control", "pump:on")
    return redirect('/')

@app.route('/pump/off')
def pump_off():
    print("Flask: Publishing pump:off")
    mqtt_client.publish("smartcity/control", "pump:off")
    return redirect('/')

@app.route('/pump/auto')
def pump_auto():
    print("Flask: Publishing auto")
    mqtt_client.publish("smartcity/control", "auto")
    return redirect('/')

if __name__ == '__main__':
    socketio.run(app, debug=True, port=5000)
