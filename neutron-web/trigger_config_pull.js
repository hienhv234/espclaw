const mqtt = require('mqtt');

const brokerUrl = 'tls://e855d1adcb91498097194e25175017dd.s1.eu.hivemq.cloud:8883';
const options = {
    username: 'myesp1456',
    password: 'Myesp1456',
    clientId: 'trigger_agent_' + Math.random().toString(16).substr(2, 8)
};

console.log("Connecting to HiveMQ Cloud broker...");
const client = mqtt.connect(brokerUrl, options);

client.on('connect', () => {
    console.log("Connected successfully!");
    const topic = 'espclaw/GETAI-QCSQC/cmd';
    const payload = JSON.stringify({ action: 'pull_config' });
    
    console.log(`Publishing pull_config payload to topic: ${topic}`);
    client.publish(topic, payload, { qos: 1 }, (err) => {
        if (err) {
            console.error("Publish failed:", err);
        } else {
            console.log("Publish success! Device should sync now.");
        }
        client.end();
    });
});

client.on('error', (err) => {
    console.error("MQTT connection error:", err);
    client.end();
});
