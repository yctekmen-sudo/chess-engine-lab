import { dirname, resolve } from 'node:path';

const build = process.env.CHESS_LAB_BUILD ??
  resolve(import.meta.dirname, '../../build/native-debug');

export function nativeExecutable(name) {
  return resolve(build, process.platform === 'win32' ? `${name}.exe` : name);
}

export const nativeEngine = process.env.CHESS_LAB_UCI ?? nativeExecutable('chess_lab_uci');

export function sharedLibrary(name, directory = dirname(nativeEngine)) {
  const filename =
    process.platform === 'win32' ? `${name}.dll` :
    process.platform === 'darwin' ? `lib${name}.dylib` :
    `lib${name}.so`;

  return resolve(directory, filename);
}
