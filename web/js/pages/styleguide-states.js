// Every control in every state, with realistic sample data. The first unit toggle is live; the others show a fixed state.
import { initPage, renderFormats, bindUnitToggles, markReady } from '../ui.js';
import { t } from '../i18n.js';

await initPage();
renderFormats(document);
document.getElementById('f7').value = t('states.fields.value_location');
bindUnitToggles((unit) => renderFormats(document, unit));
markReady();
