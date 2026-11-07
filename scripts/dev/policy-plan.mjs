// Read the existing 54-gate CI policy. Do not maintain a second test inventory.
import {plan} from '../ci/suite.mjs';
const areas = (process.argv[2] ?? '').split(',').filter(Boolean);
if (!areas.length) throw Error('At least one Windows Quick area is required');
const steps = plan('windows', 'quick', areas);
if (!steps.some(s => s.selected)) throw Error('CI policy selected no gates');
process.stdout.write(JSON.stringify(steps.map(s => ({id: s.id, selected: s.selected}))) + '\n');
