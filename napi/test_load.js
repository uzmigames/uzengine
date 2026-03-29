const path = require('path');
const addon = require(path.join(__dirname, 'build', 'uzengine.node'));

const nativeDir = path.join(__dirname, 'build');
console.log('1. setEngineDir:', nativeDir);
addon.setEngineDir(nativeDir);

console.log('2. version:', addon.getVersion(), 'backend:', addon.getBackend());

addon.setLogCallback((level, category, message) => {
    const levels = ['TRACE','DEBUG','INFO','WARN','ERROR','FATAL'];
    console.log(`  [${levels[level]||level}] [${category}] ${message}`);
});

console.log('3. init()');
try { addon.init(); console.log('   init OK'); } catch(e) { console.error('   init FAIL:', e.message); }

console.log('4. createViewport(null, 1280, 720)');
try { addon.createViewport(null, 1280, 720); console.log('   viewport OK'); } catch(e) { console.error('   viewport FAIL:', e.message); }

console.log('5. setContentRoot');
try { addon.setContentRoot('e:\\UzEngine\\sample\\content'); console.log('   contentRoot OK'); } catch(e) { console.error('   contentRoot FAIL:', e.message); }

console.log('6. loadScene');
try { addon.loadScene('e:\\UzEngine\\sample\\content\\scenes\\pbr_showcase.uzscene'); console.log('   loadScene OK'); } catch(e) { console.error('   loadScene FAIL:', e.message); }

console.log('DONE — cleaning up');
try { addon.destroyViewport(); } catch(e) {}
try { addon.shutdown(); } catch(e) {}
