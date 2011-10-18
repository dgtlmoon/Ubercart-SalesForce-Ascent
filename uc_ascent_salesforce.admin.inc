<?php

/**
 * Store-wide coupon settings form.
 */
function uc_ascent_salesforce_settings_form() {
  global $base_url;
 
  $form['uc_ascent_salesforce_username'] = array(
    '#type' => 'textfield',
    '#title' => t('SalesForce API Username (email address)'),
    '#default_value' => variable_get('uc_ascent_salesforce_username', ''),
    '#required' => true  
  );
  
  $form['uc_ascent_salesforce_password'] = array(
    '#type' => 'textfield',
    '#title' => t('SalesForce API Password'),
    '#default_value' => variable_get('uc_ascent_salesforce_password', ''),
    '#required' => true  
  );
  
  $form['uc_ascent_salesforce_token'] = array(
    '#type' => 'textfield',
    '#title' => t('SalesForce API Token'),
    '#default_value' => variable_get('uc_ascent_salesforce_token', ''),
    '#required' => true
  );

  $form['uc_ascent_salesforce_error_notification_email'] = array(
    '#type' => 'textfield',
    '#title' => t('Email address to notify on any errors'),
    '#default_value' => variable_get('uc_ascent_salesforce_error_notification_email', ''),
    '#required' => true
  );
  
  $form['uc_ascent_salesforce_run_cron'] = array(
    '#type' => 'checkbox',
    '#title' => t('Execute transactions with Salesforce/Ascent'),
    '#description' => t('If disabled, will just collect items in the queue entered from conditional actions in Ubercart configuration'),
    '#default_value' => variable_get('uc_ascent_salesforce_run_cron', false),
  );
  
  
  
  return system_settings_form($form);
}
  