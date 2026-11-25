// Shared by the Pool Manager screens: pool labels, event words, and plain sentences for refusals. No codes are ever shown.
import { gradeLabel } from './components.js';
import { t } from './i18n.js';
import { ApiError } from './api.js';
import { poolCode } from './components.js';

// "P-12 · Paddy · Sona Masuri · Range 1"
export const poolLabel = (p) => `${poolCode(p.pool_id)} · ${p.commodity} · ${p.variety} · ${gradeLabel(p.range_no)}`;

const EVENT_WORDS = { SALE: 'event.sale', CONTRIBUTION: 'event.deposit', PRICE_CHANGED: 'event.price_change', STATUS_CHANGED: 'event.status_change', POOL_CREATED: 'event.pool_creation' };
export const eventWord = (type) => t(EVENT_WORDS[type] || 'event.record');

export const statusWord = (s) => t(`status.${s}`);

// The sentence for a failed Manager action (create, deposit, price, status, photo, account).
export function managerError(err) {
  if (!(err instanceof ApiError)) return t('error.generic');
  switch (err.code) {
    case 'DUPLICATE_USERNAME': return t('manager.err_duplicate_username');
    case 'POOL_NOT_OPEN': return t('manager.err_pool_closed');
    case 'DUPLICATE_PHONE': return t('people.err_duplicate_phone');
    case 'NO_ID_DOCUMENT': return t('people.err_no_doc');
    case 'FARMER_NOT_APPROVED':
    case 'BUYER_NOT_APPROVED': return t('manager.err_not_approved');
    case 'NO_CHANGE': return t('manager.err_no_change');
    case 'PHOTO_REQUIRED': return t('manager.err_photo_required');
    case 'PHOTO_TOO_LARGE': return t('manager.err_photo_large');
    case 'UNSUPPORTED_IMAGE':
    case 'UNSUPPORTED_MEDIA_TYPE': return t('manager.err_photo_type');
    case 'INVALID_TRANSITION': return t('manager.err_transition');
    case 'NOT_FOUND': return t('error.not_found');
    case 'INVALID_FIELD':
    case 'INVALID':
    case 'MISSING_FIELD': return t('manager.err_invalid');
    default: break;
  }
  if (err.status === 401) return t('error.session_ended');
  if (err.status === 403) return t('error.forbidden');
  if (err.status === 429) return t('login.too_many');
  return t('error.generic');
}

// The sentence for a failed integrity check: which record, in plain words.
export function integritySentence(first) {
  const params = { n: first.sequence_no, what: eventWord(first.event_type) };
  if (first.reason === 'HASH_MISMATCH') return t('manager.integrity_mismatch', params);
  if (first.reason === 'INVARIANT') return t('manager.integrity_totals', params);
  return t('manager.integrity_order', params);   // GAP or LINK_BROKEN
}

// Photo pre-check in the browser (the server checks again by content): JPEG or PNG, at most 5 MB.
export function photoProblem(file) {
  if (!['image/jpeg', 'image/png'].includes(file.type)) return t('manager.err_photo_type');
  if (file.size > 5 * 1024 * 1024) return t('manager.err_photo_large');
  return null;
}
