import { SerialPort } from 'serialport';
import { ReadlineParser } from '@serialport/parser-readline';

export async function listPorts() {
  const ports = await SerialPort.list();
  return ports.map((port) => ({
    path: port.path,
    manufacturer: port.manufacturer || '',
    friendlyName: port.friendlyName || port.path,
  }));
}

export function createSerialClient({ portPath, baudRate = 115200, onPacket, onStatus, onError }) {
  let port = null;

  const connect = () => {
    onStatus?.('connecting');
    port = new SerialPort({ path: portPath, baudRate, autoOpen: false });
    const parser = port.pipe(new ReadlineParser({ delimiter: '\n' }));

    port.open((error) => {
      if (error) {
        onError?.(error);
        onStatus?.('disconnected');
        return;
      }
      onStatus?.('connected');
    });

    parser.on('data', (line) => {
      try {
        const packet = JSON.parse(line);
        onPacket?.(packet);
      } catch (error) {
        onError?.(error);
      }
    });

    port.on('close', () => onStatus?.('disconnected'));
    port.on('error', (error) => onError?.(error));
  };

  const close = () => {
    if (port?.isOpen) {
      port.close();
    }
    port = null;
  };

  connect();

  return { close };
}
