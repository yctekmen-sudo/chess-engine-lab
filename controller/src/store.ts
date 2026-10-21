import { createHash, randomUUID } from 'node:crypto';
import { open, mkdir, readFile, readdir, rename, link, unlink } from 'node:fs/promises';
import { hostname } from 'node:os';
import { dirname, join } from 'node:path';
export function canonical(value:unknown):string {
  if(value===null||typeof value!=='object') {
    const s=JSON.stringify(value);if(s===undefined)throw Error('non-JSON data');return s;
  }
  if(Array.isArray(value))return '['+value.map(canonical).join(',')+']';
  return '{'+Object.entries(value).sort(([a],[b])=>a.localeCompare(b,'en')).map(([k,v])=>JSON.stringify(k)+':'+canonical(v)).join(',')+'}';
}
export const digest=(value:unknown):string=>createHash('sha256').update(canonical(value)).digest('hex');
export async function syncDirectory(path:string):Promise<void> {
  const fd=await open(path,'r');try{await fd.sync();}finally{await fd.close();}
}
export async function writeRecord(path:string,data:unknown,immutable=true):Promise<void> {
  await mkdir(dirname(path),{recursive:true});const temporary=join(dirname(path),'.tmp-'+randomUUID());
  const fd=await open(temporary,'wx',0o600);
  try{await fd.writeFile(canonical({schema:1,digest:digest(data),data})+'\n');await fd.sync();}finally{await fd.close();}
  try {
    if(immutable){await link(temporary,path);await unlink(temporary);}else await rename(temporary,path);
    await syncDirectory(dirname(path));
  }catch(e){await unlink(temporary).catch(()=>{});throw e;}
}
export async function readRecord<T>(path:string):Promise<T> {
  let envelope:{schema:number;digest:string;data:T};
  try{envelope=JSON.parse(await readFile(path,'utf8')) as typeof envelope;}catch{throw Error('torn or invalid record: '+path);}
  if(envelope.schema!==1)throw Error('unsupported schema; migration required: '+path);
  if(envelope.digest!==digest(envelope.data))throw Error('record integrity failure: '+path);
  return envelope.data;
}
export async function exists(path:string):Promise<boolean> {
  try{await readFile(path);return true;}catch(e){if((e as NodeJS.ErrnoException).code==='ENOENT')return false;throw e;}
}
export async function names(path:string):Promise<string[]> {
  try{return (await readdir(path)).filter(x=>!x.startsWith('.')).sort();}catch(e){if((e as NodeJS.ErrnoException).code==='ENOENT')return [];throw e;}
}
type Lock={host:string;pid:number;token:string};
export async function writerLock(directory:string,takeover=false):Promise<()=>Promise<void>> {
  await mkdir(directory,{recursive:true});const path=join(directory,'writer.lock');
  const own:Lock={host:hostname(),pid:process.pid,token:randomUUID()};
  try{await writeRecord(path,own);}catch(e){
    if((e as NodeJS.ErrnoException).code!=='EEXIST')throw e;
    if(!takeover)throw Error('writer lock exists; explicit stale-lock takeover required');
    const old=await readRecord<Lock>(path);
    if(old.host!==hostname()||!Number.isSafeInteger(old.pid)||old.pid<=0)throw Error('cannot establish stale lock ownership');
    try{process.kill(old.pid,0);throw Error('lock owner is still alive');}catch(e){if((e as NodeJS.ErrnoException).code!=='ESRCH')throw e;}
    // Exclusive takeover guard prevents two rescuers unlinking each other's lock.
    const guard=await open(join(directory,'.takeover'),'wx');
    try {
      if((await readRecord<Lock>(path)).token!==old.token)throw Error('lock changed during recovery');
      await unlink(path);await writeRecord(path,own);
    }finally{await guard.close();await unlink(join(directory,'.takeover'));}
  }
  return async()=>{if((await readRecord<Lock>(path)).token!==own.token)throw Error('lost writer lock');await unlink(path);await syncDirectory(directory);};
}
