<?php

/**
 * Store-wide coupon settings form.
 */
function uc_ascent_salesforce_settings_form() {
  global $base_url;
  ini_set("soap.wsdl_cache_enabled", 0);
  
  // ensure some settings
  if (! variable_get('uc_ascent_salesforce_wsdl_ascent', false) || ! variable_get('uc_ascent_salesforce_wsdl_salesforce', false)) {
    drupal_set_message('WSDL\'s are not configurated correctly, please select an Ascent and Salesforce Enterprise XML WSDL');
  }
  
  $form ['uc_ascent_salesforce_username'] = array (
    '#type' => 'textfield', '#title' => t('SalesForce API Username (email address)'), '#default_value' => variable_get('uc_ascent_salesforce_username', ''), '#required' => true 
  );
  
  $form ['uc_ascent_salesforce_password'] = array (
    '#type' => 'textfield', '#title' => t('SalesForce API Password'), '#default_value' => variable_get('uc_ascent_salesforce_password', ''), '#required' => true 
  );
  
  $form ['uc_ascent_salesforce_token'] = array (
    '#type' => 'textfield', '#title' => t('SalesForce API Token'), '#default_value' => variable_get('uc_ascent_salesforce_token', ''), '#required' => true 
  );
  
  $form ['uc_ascent_salesforce_error_notification_email'] = array (
    '#type' => 'textfield', '#title' => t('Email address to notify on any errors'), '#default_value' => variable_get('uc_ascent_salesforce_error_notification_email', ''), '#required' => true 
  );
  
  $form ['uc_ascent_salesforce_wsdl_salesforce'] = array (
    '#type' => 'select', '#title' => t('WSDL to use for Salesforce Enterprise'), '#description' => t('Chosen from resources/'), '#options' => _get_wsdl_in_resource_directory(), '#required' => true, '#default_value' => variable_get('uc_ascent_salesforce_wsdl_salesforce', false) 
  );
  
  $form ['uc_ascent_salesforce_wsdl_ascent'] = array (
    '#type' => 'select', '#title' => t('WSDL to use for Salesforce Ascent'), '#description' => t('Chosen from resources/'), '#options' => _get_wsdl_in_resource_directory(), '#required' => true, '#default_value' => variable_get('uc_ascent_salesforce_wsdl_ascent', false) 
  );
  
  $form ['uc_ascent_salesforce_run_cron'] = array (
    '#type' => 'checkbox', '#title' => t('Execute transactions with Salesforce/Ascent on cron/hourly'), '#description' => t('If disabled, will just collect items in the queue entered from conditional actions in Ubercart configuration and will not push to Salesforce/Ascent'), '#default_value' => variable_get('uc_ascent_salesforce_run_cron', false) 
  );
  
  return system_settings_form($form);
}

/**
 * Return an array of filenames matching .xml
 */
function _get_wsdl_in_resource_directory() {
  $return = array ();
  $return [] = '<none>';
  if ($handle = opendir(drupal_get_path('module', 'uc_ascent_salesforce') . '/resources/')) {
    while (false !== ($file = readdir($handle))) {
      if (preg_match('/xml$/i', $file)) {
        $return [$file] = $file;
      }
    }
    closedir($handle);
  }
  
  return $return;
}

function uc_ascent_salesforce_queue_info() {
  
  $header = array (
    array (
    'data' => t('Queue ID'), 'field' => 'qid' 
  ), array (
    'data' => t('Function'), 'field' => 'queue_function' 
  ), array (
    'data' => t('Order ID'), 'field' => 'order_id', 'sort' => 'desc' 
  ), array (
    'data' => t('Queue Status'), 'field' => 'q_status' 
  ), array (
    'data' => t('Processed Time'), 'field' => 'recent_request_time' 
  ), array (
    'data' => t(''),  
  ) 
  );
  
  $result = pager_query('SELECT * FROM {uc_sf_queue} ' . tablesort_sql($header), 100, 0, 2, 1);
  $rows = array ();
  while ($queue_item = db_fetch_object($result)) {
    $rows [] = array (
      $queue_item->qid, 
      check_plain($queue_item->queue_function), 
      l($queue_item->order_id, 'admin/store/orders/' . $queue_item->order_id), 
      ! $queue_item->q_status ? t('Waiting') : t('Complete'), 
      $queue_item->request_recent_time, 
      l('view transaction','admin/store/settings/uc_salesforce/queue/'.$queue_item->qid)
    );
  }
  
  if (count($rows)) {
    $output = theme('table', $header, $rows, array (
      'width' => '100%' 
    ));
    $output .= theme('pager', NULL, 20);
  }
  else {
    $output = '<p>' . t('There are currently no queued Salesforce/Ascent activities in the system') . '</p>';
  }
  
  $output .= '<p><i>' . t('Events are queued from conditional actions from Ubercart activities') . '</i></p>';
  
  return $output;
}

  

function uc_ascent_salesforce_queue_item_info($qid) {
	
  $query = db_query("SELECT * from {uc_sf_queue_log} WHERE qid=%d ORDER BY request_start_time", $qid);
	
	while($item = db_fetch_array($query)) {

    $form['result_'.$item['id']] = array(  
      '#type' => 'fieldset', 
      '#title' => $item['request_start_time'], 
      '#collapsible' => TRUE, 
      '#collapsed' => FALSE);
  
   $form['result_'.$item['id']]['request'] = array('#type'=>'textarea', 
                           '#cols'=>100, 
                           '#rows'=>15, 
                           '#weight' =>0,
                           '#value'=>$item['request_post'],
                           '#title'=>'XML Request/Post' ); 
  
  $form['result_'.$item['id']]['replay'] = array('#type'=>'textarea', 
                           '#cols'=>100, 
                           '#rows'=>15,
                           '#weight' =>1,
                           '#value'=>$item['request_reply'],
                           '#title'=>'XML Reply' ); 
	}
	
  return drupal_render($form);
}


